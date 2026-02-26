#!/usr/bin/env python3
"""
PC Shutdown Listener (FastAPI Version)
Runs on Ubuntu PC to receive shutdown commands from ESP32
Client-side does NOT need to change
"""

from fastapi import FastAPI
from fastapi.responses import JSONResponse
import subprocess
import uvicorn

app = FastAPI()


@app.post("/shutdown")
def shutdown():
    """Handle shutdown requests from ESP32"""
    print("Shutdown request received!")

    try:
        # Execute shutdown command
        subprocess.Popen(["sudo", "shutdown", "-h", "now"])

        return JSONResponse(
            status_code=200,
            content={"success": True, "message": "Shutdown initiated"},
        )

    except Exception as e:
        print(f"Error executing shutdown: {e}")

        return JSONResponse(
            status_code=500,
            content={"success": False, "error": str(e)},
        )


@app.get("/health")
def health():
    """Health check endpoint"""
    return JSONResponse(
        status_code=200,
        content={"status": "running"},
    )


if __name__ == "__main__":
    print("PC Shutdown Listener starting on port 8080...")
    print("Make sure to configure sudoers for passwordless shutdown:")
    print("  sudo visudo")
    print("  Add line: yourusername ALL=(ALL) NOPASSWD: /sbin/shutdown")

    # Run on all interfaces so ESP32 can reach it
    uvicorn.run(app, host="0.0.0.0", port=8080)