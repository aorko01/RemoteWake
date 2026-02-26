from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import secrets
import time
from datetime import datetime, timezone
import asyncio

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

wake_requests: list[dict] = []


class WakeBody(BaseModel):
    type: str = "wake"


class AckBody(BaseModel):
    id: str
    status: str


@app.get("/")
def root():
    return {
        "message": "ESP32 WOL Server",
        "status": "running",
        "pendingRequests": len(wake_requests),
    }


@app.get("/api/wake")
def get_wake(request: Request):
    print(f"[{datetime.now(timezone.utc).isoformat()}] ESP32 polling for wake requests")

    if wake_requests:
        req = wake_requests.pop(0)
        print(f"Sending {req['type']} request: {req['id']}")
        return {
            "wake": req["type"] == "wake",
            "shutdown": req["type"] == "shutdown",
            "type": req["type"],
            "id": req["id"],
            "timestamp": req["timestamp"],
            "source": req["source"],
        }

    return {"wake": False, "shutdown": False, "type": None, "id": None}


@app.post("/api/wake")
def post_wake(body: WakeBody, request: Request):
    request_id = secrets.token_hex(8)
    timestamp = datetime.now(timezone.utc).isoformat()
    source = request.client.host if request.client else "unknown"

    wake_request = {
        "id": request_id,
        "timestamp": timestamp,
        "source": source,
        "type": body.type,
    }

    wake_requests.append(wake_request)
    print(f"[{timestamp}] New {body.type} request created: {request_id} from {source}")

    return {
        "success": True,
        "id": request_id,
        "type": body.type,
        "message": f"{body.type.capitalize()} request queued",
        "position": len(wake_requests),
    }


@app.post("/api/wake/ack")
def ack_wake(body: AckBody):
    print(f"[{datetime.now(timezone.utc).isoformat()}] Acknowledgment received: {body.id} - {body.status}")
    return {"success": True, "message": "Acknowledgment received"}


@app.get("/api/status")
def status():
    now = time.time() * 1000
    return {
        "server": "running",
        "pendingRequests": len(wake_requests),
        "uptime": time.process_time(),
        "requests": [
            {
                "id": r["id"],
                "type": r["type"],
                "timestamp": r["timestamp"],
                "age": int(now - datetime.fromisoformat(r["timestamp"]).timestamp() * 1000),
            }
            for r in wake_requests
        ],
    }


async def cleanup_old_requests():
    while True:
        await asyncio.sleep(60)
        cutoff = time.time() - 5 * 60
        initial = len(wake_requests)
        wake_requests[:] = [
            r for r in wake_requests
            if datetime.fromisoformat(r["timestamp"]).timestamp() > cutoff
        ]
        removed = initial - len(wake_requests)
        if removed:
            print(f"[{datetime.now(timezone.utc).isoformat()}] Cleaned up {removed} old requests")


@app.on_event("startup")
async def startup_event():
    asyncio.create_task(cleanup_old_requests())
    print("WOL Server running on 127.0.0.1:3000")
    print(f"Current time: {datetime.now(timezone.utc).isoformat()}")
    print("Endpoints:")
    print("  GET  /api/wake   - ESP32 polling")
    print("  POST /api/wake   - Create wake request")
    print("  GET  /api/status - Server status")


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=3000)