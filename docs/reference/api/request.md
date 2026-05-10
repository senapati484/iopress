# Request Reference

The `Request` object represents the HTTP request and has properties for the request query string, parameters, body, HTTP headers, and so on.

## Properties

### `req.method`
The HTTP method of the request (e.g., 'GET', 'POST').

### `req.path`
The path part of the request URL.

### `req.query`
An object containing a property for each query string parameter in the route.

### `req.params`
An object containing properties mapped to the named route "parameters".

### `req.headers`
An object containing the request headers.

### `req.body`
The parsed request body. By default, `@iopress/core` automatically parses `application/json` bodies.

### `req.rawBody`
A `Buffer` containing the raw, unparsed request body.

### `req.ip`
The client's IP address.
