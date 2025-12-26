# WebInk Server Protocol Specification

## 🚨 **Critical Issue: Missing Image Headers**

The WebInk client expects **standard PBM/PGM/PPM image headers** but the server currently sends **raw binary data**. This needs to be fixed on the server side.

## **Current vs Expected Protocol**

### **1. Hash Endpoint ✅ (Working)**
```
GET /get_hash?api_key=KEY&device=DEVICE&mode=MODE
Response: plain text hash (e.g., "abcd1234ef567890")
```

### **2. Image Endpoint ⚠️ (Headers Missing)**

**Client Request:**
```
GET /get_image?api_key=KEY&device=DEVICE&mode=MODE&x=0&y=START_ROW&w=800&h=NUM_ROWS&format=pbm
```

**Current Server Response (BROKEN):**
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream

[raw binary data - no headers]
```

**Expected Server Response (FIXED):**
```
HTTP/1.1 200 OK
Content-Type: image/x-portable-bitmap

P4
800 8
[binary data]
```

## **Image Format Headers Required**

The client's image parser (`webink_image.cpp`) expects these headers:

### **PBM (Monochrome - Most Common)**
```
P4          # Magic number for binary PBM
800 8       # Width Height 
[binary data - 1 bit per pixel, packed]
```

### **PGM (Grayscale)**
```
P5          # Magic number for binary PGM
800 8       # Width Height
255         # Max gray value
[binary data - 1 byte per pixel]
```

### **PPM (Color)**
```
P6          # Magic number for binary PPM  
800 8       # Width Height
255         # Max color value
[binary data - 3 bytes per pixel RGB]
```

## **Server Changes Needed**

### **Option 1: Add Headers to Existing Endpoint**
Modify `/get_image` to include proper image headers:

```python
# Before (current - BROKEN)
def get_image():
    binary_data = generate_image_slice(...)
    return binary_data

# After (fixed)
def get_image():
    width = request.args.get('w', type=int)
    height = request.args.get('h', type=int) 
    format = request.args.get('format', 'pbm')
    
    binary_data = generate_image_slice(...)
    
    if format == 'pbm':
        header = f"P4\n{width} {height}\n".encode('ascii')
        return header + binary_data
    elif format == 'pgm':
        header = f"P5\n{width} {height}\n255\n".encode('ascii') 
        return header + binary_data
    elif format == 'ppm':
        header = f"P6\n{width} {height}\n255\n".encode('ascii')
        return header + binary_data
```

### **Option 2: New Endpoint (Recommended)**
Create `/get_image_with_header` and keep old endpoint for backward compatibility:

```python
@app.route('/get_image_with_header')
def get_image_with_header():
    # Implementation with proper headers
    pass

@app.route('/get_image')  
def get_image_legacy():
    # Keep old implementation for backward compatibility
    pass
```

## **Protocol Verification Commands**

### **Test Hash Endpoint:**
```bash
curl "http://127.0.0.1:8090/get_hash?api_key=myapikey&device=test&mode=800x480x1xB"
```

### **Test Image Endpoint:**
```bash
curl "http://127.0.0.1:8090/get_image?api_key=myapikey&device=test&mode=800x480x1xB&x=0&y=0&w=800&h=8&format=pbm" > test.pbm
```

### **Verify Image Headers:**
```bash
hexdump -C test.pbm | head -5
# Should show: P4 \n 800 8 \n [binary data]
```

## **Client Testing Tools**

### **Quick Protocol Test:**
```bash
cd webink_component
make test-protocol
```

### **Server Connection Test:**
```bash  
cd webink_component
make test-server
# Or with custom server:
./test_server_connection --server "http://127.0.0.1:8090"
```

## **Socket Mode Protocol** 

For socket mode (port 8091), the protocol is:
```
Client sends: "webInkV1 API_KEY DEVICE MODE X Y W H FORMAT\n"
Server responds: [PBM header + binary data]
```

## **Impact of Fix**

✅ **With proper headers:**
- Client can parse image format correctly
- Memory optimizations work as designed  
- Zero-copy processing functions properly
- ESP32 deployment will succeed

❌ **Without headers:**
- Client image parser fails
- Memory calculations are incorrect
- ESP32 deployment will crash
- No image display functionality

## **Action Items for Server**

1. **High Priority:** Add PBM headers to `/get_image` responses
2. **Medium Priority:** Add PGM/PPM support for future color modes  
3. **Low Priority:** Optimize binary data packing for memory efficiency

The client is ready and fully tested - the server just needs to send proper image format headers!
