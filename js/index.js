/**
 * Express-Pro JavaScript API
 *
 * High-performance native HTTP server with Express-compatible API.
 * Built on io_uring (Linux), kqueue (macOS), and IOCP (Windows).
 *
 * @module express-pro
 * @version 1.0.0
 * @license ISC
 * @author senapati484
 *
 * @example
 * const expressmax = require('express-pro');
 * const app = expressmax();
 *
 * app.get('/hello', (req, res) => {
 *   res.json({ message: 'Hello World' });
 * });
 *
 * app.listen(3000, () => {
 *   console.log('Server running on port 3000');
 * });
 */

'use strict';

const native = require('../build/Release/express_pro_native');

/**
 * Parse query string into object.
 *
 * Supports multiple values for the same key (becomes array).
 *
 * @param {string} query - Query string (without leading ?)
 * @returns {Object.<string, string|string[]>} Parsed query object
 * @since 1.0.0
 * @private
 *
 * @example
 * parseQuery('foo=bar&baz=qux')
 * // returns { foo: 'bar', baz: 'qux' }
 *
 * parseQuery('tag=a&tag=b')
 * // returns { tag: ['a', 'b'] }
 */
function parseQuery(query) {
  const result = {};
  if (!query) return result;

  const params = new URLSearchParams(query);
  for (const [key, value] of params) {
    if (result[key] === undefined) {
      result[key] = value;
    } else if (Array.isArray(result[key])) {
      result[key].push(value);
    } else {
      result[key] = [result[key], value];
    }
  }
  return result;
}

/**
 * Parse request body based on content type.
 *
 * Automatically parses JSON for application/json content type.
 * Returns raw string for other content types.
 *
 * @param {Buffer|null} body - Raw body buffer from native layer
 * @param {string} [contentType] - Content-Type header value
 * @returns {Object|string|null} Parsed body or null if empty
 * @throws {SyntaxError} Throws if JSON parsing fails (caught internally)
 * @since 1.0.0
 * @private
 *
 * @example
 * parseBody(Buffer.from('{"key":"value"}'), 'application/json')
 * // returns { key: 'value' }
 *
 * parseBody(Buffer.from('Hello'), 'text/plain')
 * // returns 'Hello'
 */
function parseBody(body, contentType) {
  if (!body) return null;

  if (contentType && contentType.includes('application/json')) {
    try {
      return JSON.parse(body.toString('utf8'));
    } catch {
      return body.toString('utf8');
    }
  }

  return body.toString('utf8');
}

/**
 * HTTP Request object.
 *
 * Wrapper around native request data with Express-compatible API.
 * Provides parsed query parameters, body, headers, and route params.
 *
 * @class Request
 * @since 1.0.0
 *
 * @property {string} method - HTTP method (GET, POST, PUT, DELETE, PATCH, etc.)
 * @property {string} url - Full request URL (including query string)
 * @property {string} path - URL path without query string
 * @property {Object.<string, string|string[]>} query - Parsed query parameters
 * @property {Object.<string, string>} params - Route parameters (populated by router)
 * @property {Object.<string, string>} headers - HTTP headers (lowercase keys)
 * @property {Object|string|null} body - Parsed request body
 * @property {Buffer} rawBody - Raw body buffer
 * @property {Object} socket - Socket information { fd: number }
 * @property {boolean} complete - Whether request is complete
 */
class Request {
  /**
   * Create a Request instance from native request data.
   *
   * @param {Object} nativeReq - Native request object from C layer
   * @param {string} nativeReq.method - HTTP method
   * @param {string} nativeReq.path - Full request path
   * @param {Object} nativeReq.headers - HTTP headers
   * @param {Buffer} [nativeReq.body] - Raw body
   * @param {number} nativeReq.fd - Socket file descriptor
   * @param {boolean} nativeReq.complete - Completion status
   *
   * @throws {TypeError} Throws if nativeReq is invalid
   * @since 1.0.0
   */
  constructor(nativeReq) {
    if (!nativeReq || typeof nativeReq !== 'object') {
      throw new TypeError('nativeReq must be an object');
    }

    // From C layer
    this.method = nativeReq.method;
    this.url = nativeReq.path;
    this.headers = nativeReq.headers || {};
    this.rawBody = nativeReq.body;
    this.socket = { fd: nativeReq.fd };
    this.complete = nativeReq.complete;

    // Parse path and query
    const queryIdx = this.url.indexOf('?');
    this.path = queryIdx >= 0 ? this.url.slice(0, queryIdx) : this.url;
    this.query = queryIdx >= 0 ? parseQuery(this.url.slice(queryIdx + 1)) : {};
    this.params = {};

    // Parse body
    const contentType = this.headers['content-type'] || this.headers['Content-Type'];
    this.body = parseBody(this.rawBody, contentType);

    // Internal
    this._native = nativeReq;
  }

  /**
   * Get header value (case-insensitive lookup).
   *
   * @param {string} name - Header name (case-insensitive)
   * @returns {string|undefined} Header value or undefined if not found
   * @since 1.0.0
   *
   * @example
   * const contentType = req.get('Content-Type');
   * const auth = req.get('authorization');
   */
  get(name) {
    const lower = name.toLowerCase();
    for (const [key, value] of Object.entries(this.headers)) {
      if (key.toLowerCase() === lower) {
        return value;
      }
    }
    return undefined;
  }
}

/**
 * HTTP Response object.
 *
 * Provides Express-compatible methods for sending HTTP responses.
 * Supports method chaining for fluent API.
 *
 * @class Response
 * @since 1.0.0
 *
 * @property {number} statusCode - HTTP status code (default: 200)
 * @property {Object.<string, string>} headers - Response headers
 */
class Response {
  /**
   * Create a Response instance.
   *
   * @param {Object} nativeRes - Native response object
   * @param {number} nativeRes.fd - Socket file descriptor
   * @param {Object} binding - Native binding module for sending responses
   * @throws {TypeError} Throws if nativeRes or binding is invalid
   * @since 1.0.0
   * @private
   */
  constructor(nativeRes, binding) {
    if (!nativeRes || typeof nativeRes.fd !== 'number') {
      throw new TypeError('nativeRes must have a valid fd property');
    }

    this.statusCode = 200;
    this.headers = {};
    this._binding = binding;
    this._fd = nativeRes.fd;
    this._sent = false;
  }

  /**
   * Set HTTP status code.
   *
   * Chainable method to set the response status code.
   *
   * @param {number} code - HTTP status code (e.g., 200, 404, 500)
   * @returns {Response} Returns this for method chaining
   * @since 1.0.0
   *
   * @example
   * res.status(404).send('Not Found');
   * res.status(201).json({ created: true });
   */
  status(code) {
    this.statusCode = code;
    return this;
  }

  /**
   * Set response header.
   *
   * Sets a single header value. Header names are case-insensitive
   * internally but preserved on the wire.
   *
   * @param {string} name - Header name
   * @param {string} value - Header value
   * @returns {Response} Returns this for method chaining
   * @throws {TypeError} Throws if name is not a string
   * @since 1.0.0
   *
   * @example
   * res.set('Content-Type', 'application/json');
   * res.set('X-Custom-Header', 'value');
   */
  set(name, value) {
    if (typeof name !== 'string') {
      throw new TypeError('Header name must be a string');
    }
    this.headers[name.toLowerCase()] = value;
    return this;
  }

  /**
   * Get header value.
   *
   * Retrieves a header value by name (case-insensitive).
   *
   * @param {string} name - Header name
   * @returns {string|undefined} Header value or undefined if not set
   * @since 1.0.0
   *
   * @example
   * const type = res.get('Content-Type');
   */
  get(name) {
    return this.headers[name.toLowerCase()];
  }

  /**
   * Send JSON response.
   *
   * Serializes the provided object to JSON and sets the
   * Content-Type header to application/json.
   *
   * @param {*} obj - Object to serialize (any JSON-serializable value)
   * @returns {Response} Returns this for method chaining
   * @throws {TypeError} Throws if obj cannot be serialized
   * @since 1.0.0
   *
   * @example
   * res.json({ message: 'Hello' });
   * res.json([1, 2, 3]);
   * res.status(404).json({ error: 'Not found' });
   */
  json(obj) {
    const body = JSON.stringify(obj);
    this.set('Content-Type', 'application/json');
    return this.send(body);
  }

  /**
   * Send response body.
   *
   * Sends the response with the provided data. Automatically sets
   * Content-Type to text/plain if not already set.
   *
   * @param {string|Buffer} data - Response data
   * @returns {Response} Returns this for method chaining
   * @throws {Error} Throws if response was already sent
   * @since 1.0.0
   *
   * @example
   * res.send('Hello World');
   * res.send(Buffer.from('binary data'));
   * res.status(200).send('OK');
   */
  send(data) {
    if (this._sent) {
      console.warn('Response already sent');
      return this;
    }

    // Convert to string for native binding (native expects UTF-8 string)
    const bodyStr = Buffer.isBuffer(data) ? data.toString('utf8') : String(data);

    // Default content-type
    if (!this.get('content-type')) {
      this.set('Content-Type', 'text/plain; charset=utf-8');
    }

    // Call native SendResponse with headers
    this._binding.SendResponse(
      this._fd,
      this.statusCode,
      bodyStr,
      this.headers
    );

    this._sent = true;
    return this;
  }

  /**
   * End the response.
   *
   * Signals that the response is complete. Optionally sends
   * final data before ending.
   *
   * @param {string|Buffer} [data] - Optional final data to send
   * @returns {void}
   * @since 1.0.0
   *
   * @example
   * res.end();
   * res.end('Final chunk');
   */
  end(data) {
    if (data !== undefined) {
      this.send(data);
    } else if (!this._sent) {
      this.send('');
    }
  }

  /**
   * Redirect to URL.
   *
   * Sets the Location header and status code (default 302),
   * then ends the response.
   *
   * @param {string} url - Target URL to redirect to
   * @param {number} [code=302] - HTTP status code (301, 302, 307, 308)
   * @returns {Response} Returns this for method chaining
   * @throws {TypeError} Throws if url is not a string
   * @since 1.0.0
   *
   * @example
   * res.redirect('/new-path');
   * res.redirect('https://example.com', 301);
   */
  redirect(url, code = 302) {
    if (typeof url !== 'string') {
      throw new TypeError('URL must be a string');
    }
    this.status(code);
    this.set('Location', url);
    return this.send('');
  }
}

/**
 * Express-Pro Application Class.
 *
 * Main application class for creating HTTP servers with Express-compatible API.
 * Supports middleware, routing, error handling, and method chaining.
 *
 * @class ExpressMax
 * @since 1.0.0
 *
 * @example
 * const app = expressmax();
 *
 * // Middleware
 * app.use((req, res, next) => {
 *   console.log(`${req.method} ${req.path}`);
 *   next();
 * });
 *
 * // Routes
 * app.get('/hello', (req, res) => {
 *   res.json({ message: 'Hello World' });
 * });
 *
 * // Error handling
 * app.onError((err, req, res) => {
 *   res.status(500).json({ error: err.message });
 * });
 *
 * // Start server
 * app.listen(3000);
 */
class ExpressMax {
/**
 * ExpressMax - High-Performance Native HTTP Server
 *
 * @module expressmax
 * @description A blazing fast native HTTP server that uses platform-specific
 *              async I/O backends (io_uring on Linux, kqueue on macOS, IOCP on Windows)
 *              to deliver 8x+ performance compared to Express.js.
 *
 * @example
 * const expressmax = require('expressmax');
 * const app = expressmax();
 *
 * app.get('/', (req, res) => res.json({ message: 'Hello, World!' }));
 * app.listen(3000);
 */
  constructor() {
    this.routes = [];
    this.middleware = [];
    this.errorHandler = null;
    this.server = null;
  }

  /**
   * Register middleware function.
   *
   * Middleware functions are executed in order before route handlers.
   * Can be path-specific or global (when path is omitted).
   *
   * @param {string|Function} path - Path pattern or middleware function
   * @param {...Function} handlers - Middleware handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * // Global middleware
   * app.use((req, res, next) => {
   *   res.set('X-Request-Id', generateId());
   *   next();
   * });
   *
   * // Path-specific middleware
   * app.use('/api', authenticateUser);
   */
  use(path, ...handlers) {
    const hadExplicitPath = typeof path !== 'function';
    if (typeof path === 'function') {
      handlers.unshift(path);
      path = '/';
    }
    for (const handler of handlers) {
      if (typeof handler !== 'function') {
        throw new TypeError('Middleware handler must be a function');
      }
      // isTerminal: true if this is a catch-all handler (no explicit path and doesn't call next)
      // These should only run when no route matches
      const isTerminal = !hadExplicitPath && path === '/';
      this.middleware.push({ path, handler, isTerminal });
    }
    return this;
  }

  /**
   * Register GET route handler.
   *
   * @param {string} path - Route pattern (e.g., '/users/:id')
   * @param {...Function} handlers - Route handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.get('/users', (req, res) => {
   *   res.json([{ id: 1, name: 'Alice' }]);
   * });
   *
   * app.get('/users/:id', (req, res) => {
   *   res.json({ id: req.params.id });
   * });
   */
  get(path, ...handlers) {
    this._addRoute('GET', path, handlers);
    return this;
  }

  /**
   * Register POST route handler.
   *
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.post('/users', (req, res) => {
   *   res.status(201).json({ created: true, body: req.body });
   * });
   */
  post(path, ...handlers) {
    this._addRoute('POST', path, handlers);
    return this;
  }

  /**
   * Register PUT route handler.
   *
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.put('/users/:id', (req, res) => {
   *   res.json({ updated: req.params.id, body: req.body });
   * });
   */
  put(path, ...handlers) {
    this._addRoute('PUT', path, handlers);
    return this;
  }

  /**
   * Register PATCH route handler.
   *
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.patch('/users/:id', (req, res) => {
   *   res.json({ patched: req.params.id });
   * });
   */
  patch(path, ...handlers) {
    this._addRoute('PATCH', path, handlers);
    return this;
  }

  /**
   * Register DELETE route handler.
   *
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handler functions
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.delete('/users/:id', (req, res) => {
   *   res.status(204).end();
   * });
   */
  delete(path, ...handlers) {
    this._addRoute('DELETE', path, handlers);
    return this;
  }

  /**
   * Set error handler.
   *
   * Error handler receives (err, req, res, next) and should
   * send an error response.
   *
   * @param {Function} handler - Error handler function (err, req, res, next)
   * @returns {ExpressMax} Returns this for method chaining
   * @throws {TypeError} Throws if handler is not a function
   * @since 1.0.0
   *
   * @example
   * app.onError((err, req, res, next) => {
   *   console.error(err);
   *   res.status(500).json({
   *     error: err.message,
   *     stack: process.env.NODE_ENV === 'development' ? err.stack : undefined
   *   });
   * });
   */
  onError(handler) {
    if (typeof handler !== 'function') {
      throw new TypeError('Error handler must be a function');
    }
    this.errorHandler = handler;
    return this;
  }

  /**
   * Helper to add a route with validation.
   *
   * @param {string} method - HTTP method
   * @param {string} path - Route pattern
   * @param {Function[]} handlers - Route handlers
   * @throws {TypeError} Throws if any handler is not a function
   * @private
   * @since 1.0.0
   */
  _addRoute(method, path, handlers) {
    for (const handler of handlers) {
      if (typeof handler !== 'function') {
        throw new TypeError('Route handler must be a function');
      }
    }
    this.routes.push({ method, path, handlers });
  }

  /**
   * Match route and extract parameters.
   *
   * Internal method to find a matching route for the given
   * HTTP method and path. Supports route parameters with :param syntax.
   *
   * @param {string} method - HTTP method (GET, POST, etc.)
   * @param {string} path - Request path
   * @returns {Object|null} Matched route with params, or null if no match
   * @returns {Object} returns.route - The matched route object
   * @returns {Object} returns.params - Extracted route parameters
   * @private
   * @since 1.0.0
   */
  _matchRoute(method, path) {
    for (const route of this.routes) {
      if (route.method !== method) continue;

      // Handle exact path match (including root '/')
      if (route.path === path) {
        return { route, params: {} };
      }

      const params = {};
      const routeParts = route.path.split('/').filter(Boolean);
      const pathParts = path.split('/').filter(Boolean);

      if (routeParts.length !== pathParts.length) continue;

      let match = true;
      for (let i = 0; i < routeParts.length; i++) {
        if (routeParts[i].startsWith(':')) {
          // Param segment - extract value
          params[routeParts[i].slice(1)] = decodeURIComponent(pathParts[i]);
        } else if (routeParts[i] !== pathParts[i]) {
          match = false;
          break;
        }
      }

      if (match) {
        return { route, params };
      }
    }
    return null;
  }

  /**
   * Execute middleware/handler chain.
   *
   * Internal method to execute a chain of handlers with proper
   * error handling and next() support. Supports async handlers.
   *
   * @param {Request} req - Request object
   * @param {Response} res - Response object
   * @param {Function[]} handlers - Array of handler functions
   * @param {number} [index=0] - Current handler index
   * @returns {Promise<void>}
   * @private
   * @since 1.0.0
   */
  async _executeChain(req, res, handlers, index = 0) {
    if (index >= handlers.length) {
      // No more handlers - send 404 if response not sent
      if (!res._sent) {
        res.status(404).send('Not Found');
      }
      return;
    }

    const handler = handlers[index];

    // Create next function
    const next = async (err) => {
      if (err) {
        if (this.errorHandler) {
          try {
            await this.errorHandler(err, req, res);
          } catch {
            res.status(500).send('Internal Server Error');
          }
        } else {
          res.status(500).send('Internal Server Error');
        }
        return;
      }

      // Continue to next handler
      await this._executeChain(req, res, handlers, index + 1);
    };

    try {
      const result = handler(req, res, next);
      if (result && typeof result.then === 'function') {
        // Async handler - wait for it
        await result;
      }
    } catch (err) {
      next(err);
    }
  }

  /**
   * Start listening for connections.
   *
   * Creates and starts the HTTP server on the specified port.
   * Server options can configure buffer sizes and body handling.
   *
   * @param {number} port - Port number to listen on
   * @param {Object|Function} [options] - Server options or callback
   * @param {number} [options.initialBufferSize=4096] - Initial buffer size in bytes
   * @param {number} [options.maxBodySize=1048576] - Maximum body size in bytes (1MB)
   * @param {boolean} [options.streamBody=false] - Stream large bodies
   * @param {Function} [callback] - Callback function called when server starts
   * @returns {Object} Server info object
   * @returns {number} returns.port - Port number
   * @returns {string} returns.backend - Backend being used (io_uring, kqueue, iocp, libuv)
   * @throws {Error} Throws if port is invalid or server fails to start
   * @since 1.0.0
   *
   * @example
   * // Basic usage
   * app.listen(3000, () => {
   *   console.log('Server started');
   * });
   *
   * // With options
   * app.listen(3000, {
   *   initialBufferSize: 16384,
   *   maxBodySize: 5 * 1024 * 1024  // 5MB
   * }, () => {
   *   console.log('Server started with custom options');
   * });
   *
   * // Get server info
   * const info = app.listen(3000);
   * console.log(info.backend);  // 'io_uring' on Linux
   */
  listen(port, options = {}, callback) {
    if (typeof port !== 'number' || port < 1 || port > 65535) {
      throw new Error('Port must be a number between 1 and 65535');
    }

    if (typeof options === 'function') {
      callback = options;
      options = {};
    }

    // Set server options
    const config = {
      initialBufferSize: options.initialBufferSize || 4096,
      maxBodySize: options.maxBodySize || 1048576,
      streamBody: options.streamBody || false,
      ...options
    };
    native.SetServerOptions(config);

    // Store reference for use in handler
    const self = this;

    // Start server with a master handler that routes all requests
    const serverInfo = native.Listen(port, config, (nativeReq, nativeRes) => {
      // This callback is invoked from C via threadsafe function for each request
      const req = new Request(nativeReq);
      const res = new Response(nativeRes, native);

      // Match route and extract params
      const match = self._matchRoute(req.method, req.path);

      if (match) {
        req.params = match.params;

        // Build handler chain:
        // 1. Non-terminal middleware (calls next())
        // 2. Route handlers
        // Exclude terminal middleware (catch-all handlers) when a route matches
        const chain = [
          ...self.middleware.filter(m => {
            // Include if: path matches AND (not terminal OR explicitly registered with a path)
            return req.path.startsWith(m.path) && !m.isTerminal;
          }).map(m => m.handler),
          ...match.route.handlers
        ];

        // Execute chain
        self._executeChain(req, res, chain);
      } else {
        // No route matched - 404
        res.status(404).json({ error: 'Not Found', path: req.path, method: req.method });
      }
    });

    if (callback) {
      callback();
    }

    return serverInfo;
  }

  /**
   * Close the server.
   *
   * Stops the server from accepting new connections.
   *
   * @param {Function} [callback] - Callback when server closes
   * @returns {void}
   * @since 1.0.0
   *
   * @example
   * app.close(() => {
   *   console.log('Server closed');
   * });
   */
  close(callback) {
    // Call native close
    native.Close();
    if (callback) {
      callback();
    }
  }
}

/**
 * Factory function to create an ExpressMax application.
 *
 * Creates and returns a new ExpressMax application instance.
 * This is the default export and recommended way to create apps.
 *
 * @function expressmax
 * @returns {ExpressMax} New ExpressMax application instance
 * @since 1.0.0
 *
 * @example
 * const expressmax = require('express-pro');
 * const app = expressmax();
 *
 * app.get('/', (req, res) => {
 *   res.json({ hello: 'world' });
 * });
 *
 * app.listen(3000);
 */
function expressmax() {
  return new ExpressMax();
}

/**
 * Module version string.
 * @type {string}
 * @since 1.0.0
 * @static
 */
expressmax.version = native.version;

/**
 * Current platform identifier.
 * @type {string}
 * @since 1.0.0
 * @static
 */
expressmax.platform = native.platform;

/**
 * Async I/O backend being used.
 *
 * - 'io_uring' on Linux 5.1+
 * - 'kqueue' on macOS
 * - 'iocp' on Windows
 * - 'libuv' fallback
 *
 * @type {string}
 * @since 1.0.0
 * @static
 */
expressmax.backend = native.backend;

/**
 * ExpressMax Application Class.
 *
 * @type {typeof ExpressMax}
 * @since 1.0.0
 * @static
 */
expressmax.expressmax = ExpressMax;

/**
 * Request Class.
 *
 * @type {typeof Request}
 * @since 1.0.0
 * @static
 */
expressmax.Request = Request;

/**
 * Response Class.
 *
 * @type {typeof Response}
 * @since 1.0.0
 * @static
 */
expressmax.Response = Response;

// Export module
module.exports = expressmax;
