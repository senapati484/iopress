/**
 * iopress JavaScript Fallback API
 *
 * Fully compatible pure JavaScript implementation of @iopress/core.
 * Used automatically when native build tools/addons are not available.
 *
 * @module fallback
 */

'use strict';

const http = require('http');

function parseQuery(query) {
  const result = {};
  if (!query) return result;
  const hasPercent = query.includes('%');
  let i = 0;
  while (i < query.length) {
    let eq = query.indexOf('=', i);
    let amp = query.indexOf('&', i);
    if (amp === -1) amp = query.length;
    if (eq === -1 || eq > amp) {
      if (i < amp) {
        const rawKey = query.slice(i, amp);
        const key = hasPercent ? decodeURIComponent(rawKey) : rawKey;
        result[key] = result[key] ? [].concat(result[key], '') : '';
      }
      i = amp + 1;
      continue;
    }
    const rawKey = query.slice(i, eq);
    const rawVal = query.slice(eq + 1, amp);
    const key = hasPercent ? decodeURIComponent(rawKey) : rawKey;
    const val = hasPercent ? decodeURIComponent(rawVal) : rawVal;
    if (result[key] === undefined) {
      result[key] = val;
    } else if (Array.isArray(result[key])) {
      result[key].push(val);
    } else {
      result[key] = [result[key], val];
    }
    i = amp + 1;
  }
  return result;
}

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

class Request {
  constructor(nodeReq, rawBody) {
    this.method = nodeReq.method;
    this.url = nodeReq.url;
    this.headers = nodeReq.headers || {};
    this.rawBody = rawBody;
    this.socket = nodeReq.socket;
    this.complete = nodeReq.complete;

    const queryIdx = this.url.indexOf('?');
    this.path = queryIdx >= 0 ? this.url.slice(0, queryIdx) : this.url;
    this.query = queryIdx >= 0 ? parseQuery(this.url.slice(queryIdx + 1)) : {};
    this.params = {};

    const contentType = this.headers['content-type'] || this.headers['Content-Type'];
    this.body = parseBody(this.rawBody, contentType);

    this._nodeReq = nodeReq;
  }

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

class Response {
  constructor(nodeRes) {
    this.statusCode = 200;
    this.headers = {};
    this._nodeRes = nodeRes;
    this._sent = false;
  }

  status(code) {
    this.statusCode = code;
    return this;
  }

  set(name, value) {
    if (typeof name !== 'string') {
      throw new TypeError('Header name must be a string');
    }
    this.headers[name.toLowerCase()] = value;
    return this;
  }

  get(name) {
    return this.headers[name.toLowerCase()];
  }

  json(obj) {
    const body = JSON.stringify(obj);
    this.set('Content-Type', 'application/json');
    return this.send(body);
  }

  send(data) {
    if (this._sent) {
      console.warn('Response already sent');
      return this;
    }

    const bodyStr = Buffer.isBuffer(data) ? data : Buffer.from(String(data), 'utf8');

    if (!this.get('content-type')) {
      this.set('Content-Type', 'text/plain; charset=utf-8');
    }

    /* Write headers to Node response */
    this._nodeRes.statusCode = this.statusCode;
    for (const [key, value] of Object.entries(this.headers)) {
      this._nodeRes.setHeader(key, value);
    }

    this._nodeRes.end(bodyStr);
    this._sent = true;
    return this;
  }

  end(data) {
    if (data !== undefined) {
      this.send(data);
    } else if (!this._sent) {
      this.send('');
    }
  }

  redirect(url, code = 302) {
    if (typeof url !== 'string') {
      throw new TypeError('URL must be a string');
    }
    this.status(code);
    this.set('Location', url);
    return this.send('');
  }
}

class iopress {
  // eslint-disable-next-line no-unused-vars
  constructor(options = {}) {
    this.routes = [];
    this.middleware = [];
    this.errorHandler = null;
    this.server = null;
    this._routeCache = new Map();
    this._cacheMaxSize = 256;
    this.settings = {};
    this._locals = {};
    this.engines = {};
    this._router = this;
    this.cache = {};
  }

  set(setting, value) {
    this.settings[setting] = value;
    return this;
  }

  get(path, ...handlers) {
    if (path.startsWith('/')) {
      this._addRoute('GET', path, handlers);
      return this;
    }
    if (this.settings[path] !== undefined) {
      return this.settings[path];
    }
    if (path === 'env') {
      return process.env.NODE_ENV || 'development';
    }
    return undefined;
  }

  post(path, ...handlers) {
    this._addRoute('POST', path, handlers);
    return this;
  }

  put(path, ...handlers) {
    this._addRoute('PUT', path, handlers);
    return this;
  }

  patch(path, ...handlers) {
    this._addRoute('PATCH', path, handlers);
    return this;
  }

  delete(path, ...handlers) {
    this._addRoute('DELETE', path, handlers);
    return this;
  }

  onError(handler) {
    if (typeof handler !== 'function') {
      throw new TypeError('Error handler must be a function');
    }
    this.errorHandler = handler;
    return this;
  }

  all(path, ...handlers) {
    const methods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'HEAD', 'OPTIONS'];
    for (const method of methods) {
      this._addRoute(method, path, handlers);
    }
    return this;
  }

  param(name, handler) {
    if (!this.params) this.params = {};
    this.params[name] = handler;
    return this;
  }

  head(path, ...handlers) {
    this._addRoute('HEAD', path, handlers);
    return this;
  }

  options(path, ...handlers) {
    this._addRoute('OPTIONS', path, handlers);
    return this;
  }

  use(pathOrHandler, ...handlers) {
    let path = '/';
    let fns = [];

    if (typeof pathOrHandler === 'string') {
      path = pathOrHandler;
      fns = handlers;
    } else if (typeof pathOrHandler === 'function') {
      fns = [pathOrHandler, ...handlers];
    }

    for (const fn of fns) {
      if (typeof fn !== 'function') {
        throw new TypeError('Middleware must be a function');
      }
      const isErrorHandler = fn.length === 4;
      this.middleware.push({
        path,
        handler: fn,
        isTerminal: false,
        isErrorHandler
      });
    }
    return this;
  }

  _addRoute(method, path, handlers) {
    for (const handler of handlers) {
      if (typeof handler !== 'function') {
        throw new TypeError('Route handler must be a function');
      }
    }
    this.routes.push({ method, path, handlers });
  }

  _matchRoute(method, path) {
    const cacheKey = method + '|' + path;
    if (this._routeCache.has(cacheKey)) {
      return this._routeCache.get(cacheKey);
    }

    for (const route of this.routes) {
      if (route.method !== method) continue;

      if (route.path === path) {
        const result = { route, params: {} };
        if (this._routeCache.size >= this._cacheMaxSize) {
          const keys = Array.from(this._routeCache.keys());
          for (const k of keys.slice(0, 64)) this._routeCache.delete(k);
        }
        this._routeCache.set(cacheKey, result);
        return result;
      }

      const params = {};
      const routeParts = route.path.split('/').filter(Boolean);
      const pathParts = path.split('/').filter(Boolean);

      if (routeParts.length !== pathParts.length) continue;

      let match = true;
      for (let i = 0; i < routeParts.length; i++) {
        if (routeParts[i].startsWith(':')) {
          params[routeParts[i].slice(1)] = decodeURIComponent(pathParts[i]);
        } else if (routeParts[i] !== pathParts[i]) {
          match = false;
          break;
        }
      }

      if (match) {
        const result = { route, params };
        if (this._routeCache.size >= this._cacheMaxSize) {
          const keys = Array.from(this._routeCache.keys());
          for (const k of keys.slice(0, 64)) this._routeCache.delete(k);
        }
        this._routeCache.set(cacheKey, result);
        return result;
      }
    }
    return null;
  }

  _executeChain(req, res, handlers) {
    let index = 0;
    const self = this;

    function next(err) {
      if (err) {
        if (self.errorHandler) {
          try {
            const result = self.errorHandler(err, req, res);
            if (result && typeof result.then === 'function') {
              result.catch(_e => {
                if (!res._sent) res.status(500).send('Internal Server Error');
              });
            }
          } catch {
            if (!res._sent) res.status(500).send('Internal Server Error');
          }
        } else {
          if (!res._sent) res.status(500).send('Internal Server Error');
        }
        return;
      }

      if (index >= handlers.length) {
        if (!res._sent) {
          res.status(404).json({ error: 'Not Found', path: req.path, method: req.method });
        }
        return;
      }

      const handler = handlers[index++];
      try {
        const result = handler(req, res, next);
        if (result && typeof result.then === 'function') {
          result.catch(next);
        }
      } catch (err) {
        next(err);
      }
    }

    next();
  }

  listen(port, options = {}, callback) {
    if (typeof port !== 'number' || port < 1 || port > 65535) {
      throw new Error('Port must be a number between 1 and 65535');
    }

    if (typeof options === 'function') {
      callback = options;
      options = {};
    }

    const self = this;

    function matchMiddleware(mPath, routePath) {
      if (mPath === '/') return true;
      if (routePath === mPath) return true;
      if (routePath.startsWith(mPath)) {
        const nextChar = routePath[mPath.length];
        return nextChar === '/' || mPath.endsWith('/');
      }
      return false;
    }

    /* Pre-compile chains for all routes to bypass dynamic allocations */
    for (let r = 0; r < this.routes.length; r++) {
      const route = this.routes[r];
      const chain = [];
      for (let i = 0; i < this.middleware.length; i++) {
        const m = this.middleware[i];
        if (matchMiddleware(m.path, route.path)) {
          chain.push(m.handler);
        }
      }
      for (let i = 0; i < route.handlers.length; i++) {
        chain.push(route.handlers[i]);
      }
      route.chain = chain;
    }

    this.server = http.createServer((nodeReq, nodeRes) => {
      const chunks = [];
      nodeReq.on('data', chunk => chunks.push(chunk));
      nodeReq.on('end', () => {
        const rawBody = Buffer.concat(chunks);
        const req = new Request(nodeReq, rawBody);
        const res = new Response(nodeRes);

        const match = self._matchRoute(req.method, req.path);

        if (match) {
          req.params = match.params;

          let chain = match.route.chain;
          if (!chain) {
            chain = [];
            for (let i = 0; i < self.middleware.length; i++) {
              const m = self.middleware[i];
              if (req.path.startsWith(m.path) && !m.isTerminal) {
                chain.push(m.handler);
              }
            }
            for (let i = 0; i < match.route.handlers.length; i++) {
              chain.push(match.route.handlers[i]);
            }
            match.route.chain = chain;
          }

          self._executeChain(req, res, chain);
        } else {
          res.status(404).json({ error: 'Not Found', path: req.path, method: req.method });
        }
      });
    });

    this.server.listen(port, () => {
      if (callback) callback();
    });

    return {
      port,
      backend: 'javascript',
      mode: 'fallback'
    };
  }

  close(callback) {
    if (this.server) {
      this.server.close(callback);
    } else if (callback) {
      callback();
    }
  }
}

function createiopress(options = {}) {
  return new iopress(options);
}

createiopress.version = '1.0.5';
createiopress.platform = 'fallback';
createiopress.backend = 'javascript';
createiopress.iopress = iopress;
createiopress.Request = Request;
createiopress.Response = Response;

module.exports = createiopress;
