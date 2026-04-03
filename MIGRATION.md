# Migration Guide: Express.js → iopress

This guide helps you migrate existing Express.js applications to iopress. Most applications require minimal changes.

**Estimated migration time:** 30 minutes to 2 hours depending on application complexity.

## Quick Reference

| Feature | Express.js | iopress | Action |
|---------|-----------|-------------|--------|
| Import | `require('express')` | `require('iopress')` | ✅ Replace |
| App creation | `express()` | `iopress()` | ⚠️  Update |
| Route handlers | `app.get()` | `app.get()` | ✅ Same |
| Middleware | `app.use()` | `app.use()` | ✅ Same |
| Route params | `req.params.id` | `req.params.id` | ✅ Same |
| Query params | `req.query` | `req.query` | ✅ Same |
| JSON responses | `res.json()` | `res.json()` | ✅ Same |
| Status codes | `res.status(404)` | `res.status(404)` | ✅ Same |
| Body parsing | `express.json()` | Built-in | ⚠️  Remove middleware |
| Static files | `express.static()` | Not included | ❌ Manual implementation |
| View engines | `app.set('view engine')` | Not supported | ❌ Use API-only approach |
| Sessions | `express-session` | Not included | ❌ External service |
| Cookies | `cookie-parser` | Not included | ❌ Manual parsing |

## Step-by-Step Migration

### Step 1: Install Dependencies

```bash
# Remove Express (optional, or keep for gradual migration)
npm uninstall express

# Install iopress
npm install iopress

# Build native addon
npm run build
```

### Step 2: Update Imports

**Before (Express.js):**
```javascript
const express = require('express');
const app = express();
```

**After (iopress):**
```javascript
const iopress = require('iopress');
const app = iopress();
```

### Step 3: Remove Body Parsing Middleware

iopress has built-in body parsing. Remove these lines:

**Before:**
```javascript
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
```

**After:**
```javascript
// No body parsing middleware needed - built into iopress
```

### Step 4: Handle Static Files

iopress does not include `express.static`. Options:

**Option A: Use a CDN (Recommended for Production)**
```javascript
// Serve static assets from CDN
// No server code needed
```

**Option B: Manual Implementation**
```javascript
const fs = require('fs');
const path = require('path');

app.get('/static/*', (req, res) => {
  const filePath = path.join(__dirname, 'public', req.params[0]);
  const ext = path.extname(filePath);

  const mimeTypes = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.gif': 'image/gif'
  };

  if (!fs.existsSync(filePath)) {
    return res.status(404).send('Not found');
  }

  const content = fs.readFileSync(filePath);
  res.set('Content-Type', mimeTypes[ext] || 'application/octet-stream');
  res.send(content);
});
```

**Option C: Use a Reverse Proxy**
```nginx
# nginx configuration
location /static/ {
    alias /var/www/static/;
    expires 1y;
}

location / {
    proxy_pass http://localhost:3000;
}
```

### Step 5: Handle View Engines

iopress does not support template engines (`res.render`). Convert to API responses:

**Before:**
```javascript
app.set('view engine', 'pug');

app.get('/user/:id', (req, res) => {
  const user = getUser(req.params.id);
  res.render('user', { user });
});
```

**After:**
```javascript
// Serve a static HTML file that fetches data via API
app.get('/user/:id', (req, res) => {
  const user = getUser(req.params.id);
  res.json(user);
});

// Or use a separate frontend framework (React, Vue, etc.)
```

### Step 6: Handle Cookies

iopress does not include cookie parsing:

```javascript
// Manual cookie parsing
function parseCookies(header) {
  const cookies = {};
  if (header) {
    header.split(';').forEach(cookie => {
      const [name, ...value] = cookie.trim().split('=');
      cookies[name] = decodeURIComponent(value.join('='));
    });
  }
  return cookies;
}

app.use((req, res, next) => {
  req.cookies = parseCookies(req.get('Cookie'));
  next();
});
```

### Step 7: Handle Sessions

Use an external session store (Redis, database) instead of in-memory sessions:

```javascript
const sessionStore = new Map(); // Replace with Redis in production

app.use((req, res, next) => {
  const sessionId = req.cookies?.sessionId || generateSessionId();
  req.session = sessionStore.get(sessionId) || {};

  res.setSession = (data) => {
    sessionStore.set(sessionId, { ...req.session, ...data });
    res.set('Set-Cookie', `sessionId=${sessionId}; HttpOnly; Secure`);
  };

  next();
});
```

## Complete Example: Before and After

### Before: Express.js App

```javascript
const express = require('express');
const path = require('path');
const cookieParser = require('cookie-parser');

const app = express();

// Middleware
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use(cookieParser());
app.use(express.static(path.join(__dirname, 'public')));

// View engine
app.set('view engine', 'pug');
app.set('views', path.join(__dirname, 'views'));

// Routes
app.get('/', (req, res) => {
  res.render('index', { title: 'Home' });
});

app.get('/api/users/:id', (req, res) => {
  const user = {
    id: req.params.id,
    visited: req.cookies.visited || 'never'
  };
  res.json(user);
});

app.post('/api/users', (req, res) => {
  const user = req.body;
  // Create user...
  res.status(201).json(user);
});

// Start server
app.listen(3000, () => {
  console.log('Express server on port 3000');
});
```

### After: iopress App

```javascript
const iopress = require('iopress');
const fs = require('fs');
const path = require('path');

const app = iopress();

// Cookie parsing middleware (manual)
function parseCookies(header) {
  const cookies = {};
  if (header) {
    header.split(';').forEach(cookie => {
      const [name, ...value] = cookie.trim().split('=');
      cookies[name] = decodeURIComponent(value.join('='));
    });
  }
  return cookies;
}

app.use((req, res, next) => {
  req.cookies = parseCookies(req.get('Cookie'));
  next();
});

// Static file serving (manual)
app.get('/static/*', (req, res) => {
  const filePath = path.join(__dirname, 'public', req.params[0]);
  if (!fs.existsSync(filePath)) {
    return res.status(404).send('Not found');
  }
  res.send(fs.readFileSync(filePath));
});

// Routes (no body parsing middleware needed)
app.get('/', (req, res) => {
  // Serve static HTML instead of templating
  const html = fs.readFileSync(path.join(__dirname, 'public', 'index.html'));
  res.set('Content-Type', 'text/html');
  res.send(html);
});

app.get('/api/users/:id', (req, res) => {
  const user = {
    id: req.params.id,
    visited: req.cookies.visited || 'never'
  };
  res.json(user);
});

app.post('/api/users', (req, res) => {
  const user = req.body; // Automatically parsed
  // Create user...
  res.status(201).json(user);
});

// Error handling
app.onError((err, req, res) => {
  console.error(err);
  res.status(500).json({ error: err.message });
});

// Start server
app.listen(3000, () => {
  console.log('iopress server on port 3000');
  console.log('Backend:', app.backend || 'unknown');
});
```

## Platform-Specific Performance

iopress automatically selects the best async I/O backend for your platform:

| Platform | Backend | Expected Performance |
|----------|---------|---------------------|
| Linux 5.1+ | io_uring | **500,000+ req/s** |
| macOS | kqueue | 150,000+ req/s |
| Windows | IOCP | 100,000+ req/s |
| Other | libuv | 40,000+ req/s |

### Graceful Degradation

Your application works identically across all platforms. The backend selection is transparent:

```javascript
const app = iopress();

// Check which backend is being used
console.log('Backend:', iopress.backend);
// Linux: 'io_uring'
// macOS: 'kqueue'
// Windows: 'iocp'
// Other: 'libuv'
```

### Optimizing for Production

**Linux (Recommended for Maximum Performance):**
```bash
# Check kernel version
uname -r  # Need 5.1+

# Install liburing
sudo apt-get install liburing-dev  # Debian/Ubuntu
sudo dnf install liburing-devel    # RHEL/Fedora

# Rebuild for production
npm run build
```

**All Platforms:**
```javascript
// Tune for your workload
const app = iopress({
  initialBufferSize: 65536,  // For large headers
  maxBodySize: 10 * 1024 * 1024  // 10MB for file uploads
});
```

## Common Migration Issues

### Issue: `res.render is not a function`

**Cause:** iopress does not support template engines.

**Solution:** Use static HTML files or a separate frontend framework.

### Issue: `req.cookies is undefined`

**Cause:** Cookie parsing is not built-in.

**Solution:** Implement manual cookie parsing or use a CDN for auth.

### Issue: Body not parsed in POST requests

**Cause:** Old Express body parsing middleware interfering.

**Solution:** Remove `express.json()` middleware - body parsing is built-in.

### Issue: Static files return 404

**Cause:** `express.static` is not available.

**Solution:** Implement manual file serving or use a CDN/reverse proxy.

## Testing After Migration

1. **Start the server:**
   ```bash
   npm run build
   node app.js
   ```

2. **Test basic routes:**
   ```bash
   curl http://localhost:3000/health
   curl http://localhost:3000/api/users
   ```

3. **Test POST with body:**
   ```bash
   curl -X POST http://localhost:3000/api/users \
     -H "Content-Type: application/json" \
     -d '{"name":"Test"}'
   ```

4. **Benchmark performance:**
   ```bash
   npm run bench:regression
   ```

## Gradual Migration Strategy

For large applications, consider gradual migration:

1. **Start with non-critical routes** (health checks, static data)
2. **Run both servers** behind a load balancer
3. **Route by path prefix:**
   ```nginx
   location /api/v2/ {
       proxy_pass http://iopress:3000;
   }
   location / {
       proxy_pass http://express-legacy:3001;
   }
   ```
4. **Migrate one endpoint at a time**

## Support

- **Documentation:** [README.md](./README.md)
- **Examples:** [examples/](./examples/)
- **Issues:** https://github.com/senapati484/iopress/issues

---

**Migration complete!** Your application should now be running on iopress with significantly improved performance on Linux.
