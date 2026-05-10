# API Reference Overview

This section provides detailed technical documentation for all classes, methods, and types available in **@iopress/core**.

## Core Components

- [**Application**](application.md) - The main `@iopress/core` application class.
- [**Request**](request.md) - The HTTP request object.
- [**Response**](response.md) - The HTTP response object.
- [**Settings**](settings.md) - Configuration options for the server engine.

## Module Exports

When you `require('@iopress/core')`, you get a function that creates an application instance, along with several utility properties.

```javascript
const iopress = require('@iopress/core');

// Create an app
const app = iopress({ ...options });

// Utility properties
console.log(iopress.version);   // Current version string
console.log(iopress.platform);  // 'linux', 'darwin', or 'win32'
console.log(iopress.backend);   // 'io_uring', 'kqueue', 'iocp', or 'libuv'
```

### `iopress(options?)`

Creates a new instance of the `@iopress/core` application.

- **Arguments:**
    - `options`: (Optional) An object of [Settings](settings.md).
- **Returns:** An instance of [iopressClass](application.md).

## Common Types

### `RequestHandler`
A function that handles a request.
`(req: Request, res: Response, next: NextFunction) => void | Promise<void>`

### `ErrorRequestHandler`
A function that handles errors.
`(err: Error, req: Request, res: Response, next: NextFunction) => void | Promise<void>`

### `NextFunction`
A function to pass control to the next handler.
`(err?: Error) => void`
