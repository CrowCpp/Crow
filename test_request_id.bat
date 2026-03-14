@echo off
echo.
echo === Testing Request ID Middleware ===
echo.

echo 1. Testing health endpoint (auto-generated request ID):
curl -i http://localhost:18080/health
echo.
echo.

echo 2. Testing health endpoint with custom X-Request-Id:
curl -i -H "X-Request-Id: my-custom-request-id-12345" http://localhost:18080/health
echo.
echo.

echo 3. Testing root endpoint with custom X-Request-Id:
curl -i -H "X-Request-Id: test-request-abc123" http://localhost:18080/
echo.
echo.

echo 4. Testing json endpoint (auto-generated request ID):
curl -i http://localhost:18080/json
echo.
echo.

echo === Test Complete ===
echo.
