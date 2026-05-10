# Response Reference

The `Response` object represents the HTTP response that an `@iopress/core` app sends when it gets an HTTP request.

## Methods

### `res.status(code)`
Sets the HTTP status for the response.

- **Returns:** `this` (Chainable).

### `res.json(body)`
Sends a JSON response. This method sends a response (with the correct content-type) that is the parameter converted to a JSON string using `JSON.stringify()`.

- **Returns:** `this` (Chainable).

### `res.send(body)`
Sends the HTTP response. The `body` parameter can be a `Buffer` object, a `String`, or an `Object`.

- **Returns:** `this` (Chainable).

### `res.set(name, [value])`
Sets the response's HTTP header `name` to `value`. To set multiple fields at once, pass an object as the only parameter.

```javascript
res.set('Content-Type', 'text/plain');
res.set({
  'X-Custom-Header': 'value',
  'X-Another': 'something'
});
```

### `res.get(name)`
Returns the HTTP response header specified by `name`.

### `res.redirect(url, [statusCode])`
Redirects to the URL derived from the specified `url`, with specified `statusCode` (defaults to 302).

### `res.end([data])`
Ends the response process. Use this method to quickly end the response without any data.

```javascript
res.status(404).end();
```
