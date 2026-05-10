# Engine Settings Reference

When creating an `@iopress/core` application, you can pass an optional `options` object to the constructor.

```javascript
const app = iopress({
  initialBufferSize: 32768,
  maxBodySize: 5242880
});
```

## Options

### `initialBufferSize`
- **Type:** `number`
- **Default:** `16384` (16 KB)
- **Description:** The initial size of the buffer allocated for each connection. Increase this if you expect very large request headers or small response bodies that you want to send in a single chunk.

### `maxBodySize`
- **Type:** `number`
- **Default:** `1048576` (1 MB)
- **Description:** The maximum allowed size for a request body in bytes. Requests exceeding this limit will receive a `413 Payload Too Large` response.

### `streamBody`
- **Type:** `boolean`
- **Default:** `false`
- **Description:** If set to `true`, large request bodies will be streamed instead of fully buffered. This is useful for handling large file uploads without consuming excessive memory.
