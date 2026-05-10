# Application Reference

The `iopressClass` is the heart of your application. It provides methods for routing, middleware registration, and starting the server.

## Properties

### `app.version` (read-only)
Returns the current version of the `@iopress/core` module.

### `app.platform` (read-only)
Returns the operating system platform name ('linux', 'darwin', or 'win32').

### `app.backend` (read-only)
Returns the name of the active async I/O backend ('io_uring', 'kqueue', 'iocp', or 'libuv').

## Methods

### `app.use([path], ...handlers)`
Registers middleware for the application.

- **Arguments:**
    - `path`: (Optional) The path for which the middleware is invoked (default: '*').
    - `handlers`: One or more [RequestHandler](index.md#requesthandler) functions.

### `app.get(path, ...handlers)`
Registers a GET route.

### `app.post(path, ...handlers)`
Registers a POST route.

### `app.put(path, ...handlers)`
Registers a PUT route.

### `app.delete(path, ...handlers)`
Registers a DELETE route.

### `app.patch(path, ...handlers)`
Registers a PATCH route.

### `app.all(path, ...handlers)`
Registers a handler for all HTTP methods on a specific path.

### `app.listen(port, [host], [callback])`
Starts the HTTP server.

- **Arguments:**
    - `port`: The port number to listen on.
    - `host`: (Optional) The host name or IP address (default: '0.0.0.0').
    - `callback`: (Optional) A function called once the server is listening.
- **Returns:** A `ServerInfo` object containing `port`, `host`, `running`, and `backend`.

### `app.close([callback])`
Stops the server from accepting new connections.

### `app.onError(handler)`
Registers a global error handler.

- **Arguments:**
    - `handler`: An [ErrorRequestHandler](index.md#errorrequesthandler) function.
