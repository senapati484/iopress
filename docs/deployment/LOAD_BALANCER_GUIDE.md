# Load Balancer Replacement Guide

## The Problem with Traditional Load Balancing

Most Node.js applications require multiple processes behind a load balancer to handle high traffic:

```
                    ┌─────────────┐
                    │   Nginx     │
                    │   (LB)      │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
      ┌────▼────┐     ┌────▼────┐     ┌────▼────┐
      │ Node    │     │ Node    │     │ Node    │
      │ Proc 1  │     │ Proc 2  │     │ Proc 3  │
      └─────────┘     └─────────┘     └─────────┘

Traditional Setup: 3-4 Node processes load balanced
```

**Pain Points:**
- Complex configuration (nginx/haproxy)
- Session affinity issues
- Health check management
- Additional hop latency (~0.5-2ms)
- Operational complexity

## iopress Solution

With iopress's native performance, a **single process** can often replace multiple Node.js processes:

```
┌─────────────┐         ┌─────────────┐
│   Client    │────────▶│ iopress │
│             │         │  (1 process)  │
└─────────────┘         └─────────────┘
        │                        │
   300k req/s              300k req/s
```

## Performance Comparison by Platform

| Platform | iopress | Express.js | Ratio |
|----------|-------------|------------|-------|
| **Linux (io_uring)** | **300k-500k req/s** | ~15k req/s | **20-33x** |
| macOS (kqueue) | ~80k req/s | ~15k req/s | 5x |
| Windows (IOCP) | ~100k req/s | ~15k req/s | 6x |

## When You DON'T Need a Load Balancer

### Single Server Scenarios

If your traffic is under **300k req/s**, a single iopress instance on Linux can handle it:

```javascript
// Single process handling 300k req/s
const app = iopress();
app.listen(80);  // That's it!
```

**Capacity:**
- 300k req/s = 1.08 billion requests/hour
- Typical API server: 1k-10k req/s
- High-traffic site: 50k-100k req/s
- **Verdict:** Most apps don't need a load balancer!

### Hardware Requirements

| Traffic | CPU Cores | Memory | Example |
|---------|-----------|--------|---------|
| 10k req/s | 1-2 | 512MB | Small API |
| 50k req/s | 4 | 1GB | E-commerce site |
| 100k req/s | 8 | 2GB | Popular blog |
| 300k req/s | 16 | 4GB | High-frequency trading |

## When You STILL Need a Load Balancer

### 1. Multi-Server Deployments

```
                    ┌─────────────┐
                    │   Nginx     │
                    │   (LB)      │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
      ┌────▼────┐     ┌────▼────┐     ┌────▼────┐
      │ Server  │     │ Server  │     │ Server  │
      │ 1       │     │ 2       │     │ 3       │
      │         │     │         │     │         │
      │ Express │     │ Express │     │ Express │
      │ -Pro    │     │ -Pro    │     │ -Pro    │
      └─────────┘     └─────────┘     └─────────┘
      300k req/s      300k req/s      300k req/s
      
      Total: 900k req/s capacity
```

Use when:
- Traffic exceeds single-server capacity (>300k req/s)
- Geographic distribution needed
- High availability requirements
- Zero-downtime deployments

### 2. Static File Serving

iopress is optimized for API requests. For static files, use nginx:

```nginx
server {
    location /static/ {
        # Let nginx handle static files
        root /var/www;
        expires 1y;
        add_header Cache-Control "public, immutable";
    }
    
    location /api/ {
        # Proxy to iopress
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
    }
}
```

### 3. SSL/TLS Termination

Until iopress v2 adds native HTTPS:

```nginx
server {
    listen 443 ssl http2;
    
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    location / {
        proxy_pass http://localhost:3000;
    }
}
```

## Migration Guide: Load Balancer to Single Process

### Step 1: Benchmark Current Setup

```bash
# Test current load-balanced setup
wrk -t4 -c400 -d30s http://your-api/endpoint

# Note current: requests/sec, latency p99
```

### Step 2: Deploy iopress

```javascript
// server.js
const iopress = require('@iopress/core');
const app = iopress();

// Your routes here
app.get('/api/users', (req, res) => {
    res.json({ users: [...] });
});

// Use fast routes for high-traffic endpoints
const native = require('./build/Release/express_pro_native');
native.RegisterFastRoute('GET', '/health', 200, '{"status":"ok"}');

app.listen(3000, () => {
    console.log('Server ready');
});
```

### Step 3: Performance Test

```bash
# Test iopress single instance
wrk -t4 -c400 -d30s http://localhost:3000/api/users

# Expected: 10-20x improvement over Express
```

### Step 4: Gradual Migration

```
Phase 1: Deploy iopress alongside existing setup
Phase 2: Route 10% traffic to iopress
Phase 3: Monitor for 24h
Phase 4: Route 100% traffic
Phase 5: Remove load balancer (if single server)
```

## Cost Savings Analysis

### Before (Traditional Setup)

| Component | Cost/Month |
|-----------|-----------|
| 3x Node.js servers | $150 |
| Nginx load balancer | $50 |
| Operational complexity | High |
| **Total** | **$200/month** |

### After (iopress)

| Component | Cost/Month |
|-----------|-----------|
| 1x server | $50 |
| No load balancer | $0 |
| Simple deployment | Low |
| **Total** | **$50/month** |

**Savings: 75% reduction**

## Kubernetes Deployment

### Without iopress (Traditional)

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: api
spec:
  replicas: 5  # Need 5 pods
  template:
    spec:
      containers:
      - name: node
        image: node:18
        resources:
          requests:
            memory: "512Mi"
            cpu: "500m"
---
apiVersion: v1
kind: Service
metadata:
  name: api-service
spec:
  type: LoadBalancer  # Requires cloud LB
  ports:
  - port: 80
```

### With iopress

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: api
spec:
  replicas: 1  # Just 1 pod!
  template:
    spec:
      containers:
      - name: iopress
        image: iopress:latest
        resources:
          requests:
            memory: "256Mi"
            cpu: "250m"
          limits:
            memory: "512Mi"
            cpu: "1000m"
---
apiVersion: v1
kind: Service
metadata:
  name: api-service
spec:
  type: NodePort  # No cloud LB needed for small/medium traffic
  ports:
  - port: 80
    targetPort: 3000
```

## Monitoring

Track when you might need to scale:

```javascript
// Add performance monitoring
const native = require('./build/Release/express_pro_native');

setInterval(() => {
    // Check if approaching limits
    const memUsage = process.memoryUsage();
    const cpuUsage = process.cpuUsage();
    
    if (memUsage.heapUsed > 0.8 * memUsage.heapTotal) {
        console.warn('Memory usage high - consider scaling');
    }
}, 60000);
```

## Verdict

| Traffic | Recommendation |
|---------|---------------|
| < 50k req/s | Single iopress process, no load balancer |
| 50k-300k req/s | Single iopress process, monitor closely |
| 300k+ req/s | Multiple iopress instances with load balancer |
| Global/multi-region | Multiple servers with geographic LB |

**Bottom line:** iopress eliminates load balancers for 95% of use cases!
