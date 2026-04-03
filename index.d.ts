/**
 * Express-Pro - High-performance native HTTP server
 * TypeScript declarations
 */

/// <reference types="node" />

import { Buffer } from 'node:buffer';

declare namespace ExpressPro {
  interface ExpressProOptions {
    /** Initial buffer size for request/response handling (default: 16384) */
    initialBufferSize?: number;
    /** Maximum allowed body size in bytes (default: 1048576 = 1MB) */
    maxBodySize?: number;
    /** Stream large bodies instead of buffering (default: false) */
    streamBody?: boolean;
  }

  interface RequestParams {
    [key: string]: string;
  }

  interface RequestQuery {
    [key: string]: string | string[] | undefined;
  }

  interface IncomingHttpHeaders {
    [header: string]: string | string[] | undefined;
  }

  interface Request {
    /** Request method (GET, POST, etc.) */
    method: string;
    /** Request URL path */
    path: string;
    /** URL query parameters */
    query: RequestQuery;
    /** Route parameters */
    params: RequestParams;
    /** Request headers */
    headers: IncomingHttpHeaders;
    /** Request body (parsed JSON or Buffer depending on content-type) */
    body?: unknown;
    /** Raw body buffer */
    rawBody?: Buffer;
    /** Client IP address */
    ip?: string;
  }

  interface Response {
    /** HTTP status code */
    statusCode: number;
    /** Response headers */
    headers: OutgoingHttpHeaders;

    /**
     * Set status code
     * @param code HTTP status code
     */
    status(code: number): this;

    /**
     * Send JSON response
     * @param body Response body (will be JSON.stringified)
     */
    json(body: unknown): this;

    /**
     * Send text response
     * @param body Response body
     */
    send(body: string | Buffer | object): this;

    /**
     * Set header
     * @param name Header name
     * @param value Header value
     */
    set(name: string, value: string | number | string[]): this;

    /**
     * Set multiple headers
     * @param headers Headers object
     */
    set(headers: { [key: string]: string | number | string[] }): this;

    /**
     * Get header value
     * @param name Header name
     */
    get(name: string): string | string[] | undefined;

    /**
     * Redirect to URL
     * @param url Target URL
     * @param statusCode HTTP status code (default: 302)
     */
    redirect(url: string, statusCode?: number): this;

    /**
     * End response
     * @param data Optional final data chunk
     */
    end(data?: string | Buffer): void;
  }

  interface OutgoingHttpHeaders {
    [header: string]: string | number | string[] | undefined;
  }

  type NextFunction = (err?: Error) => void;
  type RequestHandler = (req: Request, res: Response, next: NextFunction) => void | Promise<void>;
  type ErrorRequestHandler = (err: Error, req: Request, res: Response, next: NextFunction) => void | Promise<void>;

  type HttpMethod = 'GET' | 'POST' | 'PUT' | 'DELETE' | 'PATCH' | 'HEAD' | 'OPTIONS' | 'CONNECT' | 'TRACE';

  interface Route {
    path: string;
    method: HttpMethod;
    handlers: RequestHandler[];
  }

  interface ServerInfo {
    /** Server port number */
    port: number;
    /** Server host address */
    host: string;
    /** Whether server is currently running */
    running: boolean;
    /** Backend being used (io_uring, kqueue, iocp, libuv) */
    backend: string;
  }

  class ExpressProClass {
    /** Module version */
    readonly version: string;
    /** Current platform (linux, macos, windows) */
    readonly platform: string;
    /** Async I/O backend being used */
    readonly backend: string;

    constructor(options?: ExpressProOptions);

    /**
     * Register middleware for all routes
     * @param path Path pattern (optional, default: '*')
     * @param handlers Middleware functions
     */
    use(...handlers: RequestHandler[]): this;
    use(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register GET route handler
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    get(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register POST route handler
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    post(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register PUT route handler
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    put(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register DELETE route handler
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    delete(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register PATCH route handler
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    patch(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Register route handler for all HTTP methods
     * @param path Route path pattern
     * @param handlers Route handlers
     */
    all(path: string, ...handlers: RequestHandler[]): this;

    /**
     * Start listening for connections
     * @param port Port number
     * @param callback Optional callback when server starts
     */
    listen(port: number, callback?: () => void): ServerInfo;

    /**
     * Start listening for connections
     * @param port Port number
     * @param host Host address
     * @param callback Optional callback when server starts
     */
    listen(port: number, host: string, callback?: () => void): ServerInfo;

    /**
     * Stop the server
     * @param callback Optional callback when server stops
     */
    close(callback?: () => void): void;

    /**
     * Register error handler middleware
     * @param handler Error handler function
     */
    onError(handler: ErrorRequestHandler): this;
  }
}

/**
 * Create Express-Pro application instance
 * @param options Configuration options
 * @returns ExpressPro application instance
 */
declare function expressmax(options?: ExpressPro.ExpressProOptions): ExpressPro.ExpressProClass;

declare namespace expressmax {
  export const version: string;
  export const platform: string;
  export const backend: string;
  export const ExpressPro: typeof ExpressPro.ExpressProClass;
  export const Request: typeof ExpressPro.Request;
  export const Response: typeof ExpressPro.Response;
}

export = expressmax;
