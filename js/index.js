/**
 * Express-Pro JavaScript API
 *
 * Express-compatible wrapper around the native HTTP server.
 *
 * @module express-pro
 * @version 1.0.0
 */

'use strict';

const native = require('../build/Release/express_pro_native');

/**
 * Parse query string into object
 * @param {string} query - Query string (without ?)
 * @returns {Object} Parsed query object
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
 * Parse JSON body safely
 * @param {Buffer} body - Raw body buffer
 * @returns {Object|string} Parsed JSON or raw string
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
 * Create Request object from native data
 */
class Request {
  constructor(nativeReq) {
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
   * Get header value (case-insensitive)
   * @param {string} name - Header name
   * @returns {string|undefined} Header value
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
 * Response object for sending HTTP responses
 */
class Response {
  constructor(nativeRes, binding) {
    this.statusCode = 200;
    this.headers = {};
    this._binding = binding;
    this._fd = nativeRes.fd;
    this._sent = false;
  }

  /**
   * Set status code
   * @param {number} code - HTTP status code
   * @returns {Response} this for chaining
   */
  status(code) {
    this.statusCode = code;
    return this;
  }

  /**
   * Set header
   * @param {string} name - Header name
   * @param {string} value - Header value
   * @returns {Response} this for chaining
   */
  set(name, value) {
    this.headers[name.toLowerCase()] = value;
    return this;
  }

  /**
   * Get header value
   * @param {string} name - Header name
   * @returns {string|undefined} Header value
   */
  get(name) {
    return this.headers[name.toLowerCase()];
  }

  /**
   * Send JSON response
   * @param {*} obj - Object to serialize
   * @returns {Response} this for chaining
   */
  json(obj) {
    const body = JSON.stringify(obj);
    this.set('Content-Type', 'application/json');
    return this.send(body);
  }

  /**
   * Send response body
   * @param {string|Buffer} data - Response data
   * @returns {Response} this for chaining
   */
  send(data) {
    if (this._sent) {
      console.warn('Response already sent');
      return this;
    }

    // Convert to string for native binding
    const bodyStr = Buffer.isBuffer(data) ? data.toString('utf8') : String(data);

    // Default content-type
    if (!this.get('content-type')) {
      this.set('Content-Type', 'text/plain; charset=utf-8');
    }

    // Call native SendResponse
    this._binding.SendResponse(
      this._fd,
      this.statusCode,
      bodyStr
    );

    this._sent = true;
    return this;
  }

  /**
   * End response (optional final data)
   * @param {string|Buffer} [data] - Final data
   * @returns {void}
   */
  end(data) {
    if (data !== undefined) {
      this.send(data);
    } else if (!this._sent) {
      this.send('');
    }
  }

  /**
   * Redirect to URL
   * @param {string} url - Target URL
   * @param {number} [code=302] - Status code
   * @returns {Response} this for chaining
   */
  redirect(url, code = 302) {
    this.status(code);
    this.set('Location', url);
    return this.send('');
  }
}

/**
 * Express-Pro Application Class
 */
class ExpressPro {
  constructor() {
    this.routes = [];
    this.middleware = [];
    this.errorHandler = null;
    this.server = null;
  }

  /**
   * Register middleware
   * @param {string|Function} path - Path or handler
   * @param {...Function} handlers - Middleware functions
   * @returns {ExpressPro} this for chaining
   */
  use(path, ...handlers) {
    if (typeof path === 'function') {
      handlers.unshift(path);
      path = '/';
    }
    for (const handler of handlers) {
      this.middleware.push({ path, handler });
    }
    return this;
  }

  /**
   * Register GET route
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handlers
   * @returns {ExpressPro} this for chaining
   */
  get(path, ...handlers) {
    this.routes.push({ method: 'GET', path, handlers });
    return this;
  }

  /**
   * Register POST route
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handlers
   * @returns {ExpressPro} this for chaining
   */
  post(path, ...handlers) {
    this.routes.push({ method: 'POST', path, handlers });
    return this;
  }

  /**
   * Register PUT route
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handlers
   * @returns {ExpressPro} this for chaining
   */
  put(path, ...handlers) {
    this.routes.push({ method: 'PUT', path, handlers });
    return this;
  }

  /**
   * Register PATCH route
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handlers
   * @returns {ExpressPro} this for chaining
   */
  patch(path, ...handlers) {
    this.routes.push({ method: 'PATCH', path, handlers });
    return this;
  }

  /**
   * Register DELETE route
   * @param {string} path - Route pattern
   * @param {...Function} handlers - Route handlers
   * @returns {ExpressPro} this for chaining
   */
  delete(path, ...handlers) {
    this.routes.push({ method: 'DELETE', path, handlers });
    return this;
  }

  /**
   * Set error handler
   * @param {Function} handler - Error handler (err, req, res)
   * @returns {ExpressPro} this for chaining
   */
  onError(handler) {
    this.errorHandler = handler;
    return this;
  }

  /**
   * Match route and extract params
   * @param {string} method - HTTP method
   * @param {string} path - Request path
   * @returns {Object|null} Matched route or null
   */
  _matchRoute(method, path) {
    for (const route of this.routes) {
      if (route.method !== method) continue;

      const params = {};
      const routeParts = route.path.split('/').filter(Boolean);
      const pathParts = path.split('/').filter(Boolean);

      if (routeParts.length !== pathParts.length) continue;

      let match = true;
      for (let i = 0; i < routeParts.length; i++) {
        if (routeParts[i].startsWith(':')) {
          // Param segment
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
   * Execute middleware chain
   * @param {Request} req - Request object
   * @param {Response} res - Response object
   * @param {Array} handlers - Handler chain
   * @param {number} index - Current handler index
   */
  async _executeChain(req, res, handlers, index = 0) {
    if (index >= handlers.length) {
      // No more handlers
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
   * Start listening for connections
   * @param {number} port - Port number
   * @param {Object} [options] - Server options
   * @param {Function} [callback] - Callback when server starts
   * @returns {Object} Server info
   */
  listen(port, options = {}, callback) {
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

        // Build handler chain: global middleware + route handlers
        const chain = [
          ...self.middleware.filter(m => req.path.startsWith(m.path)).map(m => m.handler),
          ...match.route.handlers
        ];

        // Execute chain
        self._executeChain(req, res, chain);
      } else {
        // No route matched - 404
        res.status(404).send('Not Found');
      }
    });

    if (callback) {
      callback();
    }

    return serverInfo;
  }
}

/**
 * Factory function
 * @returns {ExpressPro} New ExpressPro instance
 */
function expressPro() {
  return new ExpressPro();
}

// Export module
module.exports = expressPro;
module.exports.ExpressPro = ExpressPro;
module.exports.Request = Request;
module.exports.Response = Response;

// Re-export native info
module.exports.version = native.version;
module.exports.platform = native.platform;
module.exports.backend = native.backend;
