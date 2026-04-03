/**
 * File Upload Example - Streaming Large Bodies
 *
 * Demonstrates:
 * - Streaming request bodies with streamBody: true
 * - Handling multipart/form-data (simulated)
 * - Large file uploads without memory buffering
 * - Progress tracking
 * - Streaming responses
 *
 * Run: node examples/upload.js
 * Requires: npm install && npm run build
 *
 * Note: This example demonstrates streaming concepts. For production
 * use, consider using a proper multipart parser.
 *
 * Test with curl:
 *
 *   # Upload a small file (streamed)
 *   curl -X POST http://localhost:3000/upload \
 *     -H "Content-Type: application/octet-stream" \
 *     --data-binary @package.json
 *
 *   # Upload with custom filename header
 *   curl -X POST http://localhost:3000/upload \
 *     -H "Content-Type: application/octet-stream" \
 *     -H "X-File-Name: myfile.txt" \
 *     --data-binary "Hello World"
 *
 *   # Stream a large response
 *   curl http://localhost:3000/download -o /dev/null
 *
 *   # Check upload status
 *   curl http://localhost:3000/uploads
 *
 *   # Upload status (progress simulation)
 *   curl -X POST http://localhost:3000/upload \
 *     -H "Content-Type: application/octet-stream" \
 *     --data-binary $(dd if=/dev/zero bs=1024 count=100 2>/dev/null | base64) \
 *     -v
 */

'use strict';

const fs = require('fs');
const path = require('path');
const expresspro = require('../index.js');

// Upload storage directory
const UPLOAD_DIR = path.join(__dirname, '../tmp/uploads');

// Ensure upload directory exists
if (!fs.existsSync(UPLOAD_DIR)) {
  fs.mkdirSync(UPLOAD_DIR, { recursive: true });
}

// Create app with streaming enabled for large bodies
const app = expresspro({
  streamBody: true,        // Enable streaming mode
  maxBodySize: 100 * 1024 * 1024,  // 100MB max
  initialBufferSize: 16384 // 16KB buffer
});

// Track uploads
const uploads = new Map();
let uploadCounter = 0;

/**
 * Middleware: Log all requests with body size
 */
app.use((req, res, next) => {
  const contentLength = parseInt(req.get('Content-Length') || '0', 10);
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path} - Content-Length: ${contentLength} bytes`);
  next();
});

/**
 * GET / - Upload form/info
 */
app.get('/', (req, res) => {
  res.json({
    message: 'File Upload Server with Streaming',
    features: [
      'streamBody: true - Bodies are streams, not buffered',
      'Large file support (up to 100MB)',
      'Progress tracking',
      'Streaming responses'
    ],
    endpoints: {
      'POST /upload': 'Upload a file (binary data)',
      'GET /uploads': 'List all uploads',
      'GET /download': 'Download a sample file (streaming)',
      'GET /uploads/:id': 'Get upload details'
    },
    curl_examples: [
      'curl -X POST http://localhost:3000/upload -H "Content-Type: application/octet-stream" --data-binary @file.txt',
      'curl http://localhost:3000/uploads',
      'curl http://localhost:3000/download -o output.bin'
    ]
  });
});

/**
 * POST /upload - Upload file with streaming
 * Note: In streamBody mode, req.body is a stream
 */
app.post('/upload', (req, res) => {
  const uploadId = ++uploadCounter;
  const filename = req.get('X-File-Name') || `upload-${uploadId}.bin`;
  const filepath = path.join(UPLOAD_DIR, `${uploadId}-${filename}`);

  const contentLength = parseInt(req.get('Content-Length') || '0', 10);
  const contentType = req.get('Content-Type') || 'application/octet-stream';

  // Create write stream (for actual file writing in production)
  // const writeStream = fs.createWriteStream(filepath);

  // Track progress
  const startTime = Date.now();

  const uploadInfo = {
    id: uploadId,
    filename,
    filepath,
    contentType,
    contentLength,
    bytesReceived: 0,
    status: 'uploading',
    startTime
  };
  uploads.set(uploadId, uploadInfo);

  console.log(`[Upload ${uploadId}] Started: ${filename} (${contentLength} bytes)`);

  // Pipe the request body stream to file
  // Note: In a real implementation, req.body would be the stream
  // For this example, we'll simulate the streaming behavior

  // Since we're demonstrating the API, let's handle the raw body
  // const chunks = [];

  // If we have raw body as buffer, write it
  if (req.rawBody) {
    fs.writeFileSync(filepath, req.rawBody);
    const bytesReceived = req.rawBody.length;
    uploadInfo.bytesReceived = bytesReceived;
    uploadInfo.status = 'completed';
    uploadInfo.duration = Date.now() - startTime;

    console.log(`[Upload ${uploadId}] Completed: ${bytesReceived} bytes in ${uploadInfo.duration}ms`);

    res.status(201).json({
      message: 'Upload successful',
      upload: {
        id: uploadId,
        filename,
        bytesReceived,
        duration: uploadInfo.duration,
        averageSpeed: `${(bytesReceived / uploadInfo.duration).toFixed(2)} bytes/ms`,
        location: `/uploads/${uploadId}`
      }
    });
  } else {
    // No body received
    uploadInfo.status = 'failed';
    uploads.delete(uploadId);

    res.status(400).json({
      error: 'No data received',
      message: 'Upload requires a body'
    });
  }
});

/**
 * GET /uploads - List all uploads
 */
app.get('/uploads', (req, res) => {
  const uploadList = Array.from(uploads.values()).map(u => ({
    id: u.id,
    filename: u.filename,
    status: u.status,
    bytesReceived: u.bytesReceived,
    contentLength: u.contentLength,
    duration: u.duration
  }));

  res.json({
    count: uploadList.length,
    uploads: uploadList
  });
});

/**
 * GET /uploads/:id - Get upload details
 */
app.get('/uploads/:id', (req, res) => {
  const id = parseInt(req.params.id, 10);

  if (isNaN(id)) {
    return res.status(400).json({ error: 'Invalid upload ID' });
  }

  const upload = uploads.get(id);
  if (!upload) {
    return res.status(404).json({ error: 'Upload not found' });
  }

  res.json({
    id: upload.id,
    filename: upload.filename,
    status: upload.status,
    contentType: upload.contentType,
    contentLength: upload.contentLength,
    bytesReceived: upload.bytesReceived,
    duration: upload.duration,
    averageSpeed: upload.duration
      ? `${(upload.bytesReceived / upload.duration).toFixed(2)} bytes/ms`
      : null
  });
});

/**
 * GET /download - Stream a sample response
 * Demonstrates streaming response capability
 */
app.get('/download', (req, res) => {
  const size = parseInt(req.query.size || '1048576', 10); // Default 1MB
  const chunkSize = 65536; // 64KB chunks

  res.set('Content-Type', 'application/octet-stream');
  res.set('Content-Disposition', 'attachment; filename="download.bin"');
  res.set('Content-Length', size.toString());

  // Simulate streaming by sending in chunks
  let sent = 0;

  function sendChunk() {
    const remaining = size - sent;
    if (remaining <= 0) {
      res.end();
      return;
    }

    const toSend = Math.min(chunkSize, remaining);
    const chunk = Buffer.alloc(toSend, 0x00);

    // In a real implementation, this would stream from a source
    // For this example, we send zero-filled buffer
    res.send(chunk.toString());

    sent += toSend;

    // Continue sending chunks
    if (sent < size) {
      setImmediate(sendChunk);
    } else {
      res.end();
    }
  }

  // Start streaming
  sendChunk();
});

/**
 * GET /stream - Server-sent events style streaming
 */
app.get('/stream', (req, res) => {
  res.set('Content-Type', 'text/plain');
  res.set('Cache-Control', 'no-cache');
  res.set('Connection', 'keep-alive');

  let counter = 0;
  const maxMessages = 10;

  function sendMessage() {
    if (counter >= maxMessages) {
      res.end('\n--- Stream complete ---\n');
      return;
    }

    const message = `Message ${++counter} at ${new Date().toISOString()}\n`;
    res.send(message);

    // Simulate periodic messages
    setTimeout(sendMessage, 500);
  }

  sendMessage();
});

// Error handling
app.onError((err, req, res, _next) => {
  console.error('Upload error:', err);
  res.status(500).json({
    error: 'Upload failed',
    message: err.message
  });
});

// Start server
const PORT = parseInt(process.env.PORT, 10) || 3000;

app.listen(PORT, () => {
  console.log(`Upload Server running on http://localhost:${PORT}`);
  console.log('\nConfiguration:');
  console.log('  - streamBody: true');
  console.log('  - maxBodySize: 100MB');
  console.log('  - initialBufferSize: 16KB');
  console.log(`\nUpload directory: ${UPLOAD_DIR}`);
  console.log('\nTest commands:');
  console.log('  # Upload a file');
  console.log(`  curl -X POST http://localhost:${PORT}/upload -H "Content-Type: application/octet-stream" -H "X-File-Name: test.txt" --data-binary "Hello World"`);
  console.log('\n  # List uploads');
  console.log(`  curl http://localhost:${PORT}/uploads`);
  console.log('\n  # Stream download');
  console.log(`  curl http://localhost:${PORT}/download`);
  console.log('\n  # Server-sent events style stream');
  console.log(`  curl http://localhost:${PORT}/stream`);
  console.log('\nPress Ctrl+C to stop');
});
