#!/usr/bin/env python3
"""MoodAnchor multi-model proxy (SiliconFlow Qwen + Coze); standard library only."""
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict, deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SILICONFLOW_API_KEY = os.environ.get("SILICONFLOW_API_KEY", "")
QWEN_MODEL = os.environ.get("QWEN_MODEL", "Qwen/Qwen3-8B")
COZE_API_KEY = os.environ.get("COZE_API_KEY", "")
COZE_BOT_ID = os.environ.get("COZE_BOT_ID", "")
COZE_API_BASE = os.environ.get("COZE_API_BASE", "https://api.coze.cn")
DEFAULT_PROVIDER = os.environ.get("DEFAULT_PROVIDER", "qwen").lower()
PORT = int(os.environ.get("PORT", "8000"))
RATE_LIMIT = int(os.environ.get("RATE_LIMIT", "20"))
visits = defaultdict(deque)
coze_conversations = {}

SYSTEM_PROMPT = (
    "你是‘是非钟’中的中文认知行为疗法（CBT）风格情绪陪伴助手。"
    "目标是帮助用户看见‘事件—自动想法—情绪/身体反应—行为’之间的联系，并把困扰转化为小而可做的行动。"
    "回答先给清楚、具体的结论或建议，再给1至3个可执行步骤；避免泛泛而谈和连续提问。"
    "只有信息不足且会实质影响建议时，最后最多追问一个问题。"
    "当用户需要安慰时，先用一句话回应其感受，再给一个当下就能做的小步骤，例如呼吸、离开压力现场、喝水、联系可信赖的人或记录想法。"
    "可温和指出认知偏差，但不要说教，不做医学诊断，不夸大传感器结果。"
    "若用户表达自伤、自杀或伤害他人的即时风险，优先建议立即联系当地急救、警方或可信赖的人，并保持简洁明确。"
)


class Handler(BaseHTTPRequestHandler):
    server_version = "MoodAnchor/0.2"

    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {
                "ok": True,
                "service": "moodanchor-multi-model",
                "default_provider": DEFAULT_PROVIDER,
                "providers": {"qwen": bool(SILICONFLOW_API_KEY), "coze": bool(COZE_API_KEY and COZE_BOT_ID)},
            })
        else:
            self.send_json(404, {"error": "not_found"})

    def do_POST(self):
        if self.path != "/chat":
            return self.send_json(404, {"error": "not_found"})
        ip = self.client_address[0]
        now = time.time()
        while visits[ip] and visits[ip][0] < now - 60:
            visits[ip].popleft()
        if len(visits[ip]) >= RATE_LIMIT:
            return self.send_json(429, {"error": "too_many_requests"})
        visits[ip].append(now)
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 16384:
                return self.send_json(400, {"error": "invalid_request_size"})
            request_data = json.loads(self.rfile.read(length).decode("utf-8"))
            message = str(request_data.get("message", "")).strip()
            if not message or len(message) > 4000:
                return self.send_json(400, {"error": "invalid_message"})
            provider = str(request_data.get("provider", DEFAULT_PROVIDER)).lower()
            if provider not in ("qwen", "coze"):
                return self.send_json(400, {"error": "invalid_provider"})
            client_id = client_identity(request_data.get("client_id"), ip)
            if provider == "qwen":
                reply, conversation_id = qwen_chat(message), None
            else:
                reply, conversation_id = coze_chat(
                    message,
                    client_id,
                    request_data.get("conversation_id"),
                    bool(request_data.get("new_conversation", False)),
                )
            payload = {"reply": reply, "provider": provider}
            if conversation_id:
                payload["conversation_id"] = conversation_id
            if provider == "coze":
                payload["bot_id"] = COZE_BOT_ID
            else:
                payload["model_id"] = QWEN_MODEL
            self.send_json(200, payload)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")[:500]
            self.send_json(502, {"error": "model_http_error", "detail": detail})
        except Exception as exc:
            self.send_json(500, {"error": "server_error", "detail": str(exc)[:500]})

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)


def qwen_chat(message):
    if not SILICONFLOW_API_KEY:
        raise RuntimeError("SILICONFLOW_API_KEY is not configured")
    payload = {
        "model": QWEN_MODEL,
        "messages": [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": message}],
        "temperature": 0.65,
        "max_tokens": 160,
        "enable_thinking": False,
    }
    request = urllib.request.Request(
        "https://api.siliconflow.cn/v1/chat/completions",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": "Bearer " + SILICONFLOW_API_KEY, "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=90) as response:
        data = json.loads(response.read().decode("utf-8"))
    reply = data["choices"][0]["message"]["content"].strip()
    if not reply:
        raise RuntimeError("Qwen returned an empty reply")
    return reply


def client_identity(value, fallback_ip):
    """Use an app-generated anonymous ID; IP is only a backward-compatible fallback.

    SSH port forwarding makes every mobile client appear as 127.0.0.1 to this server,
    so IP addresses must never be the normal Coze conversation key.
    """
    candidate = str(value or "").strip()
    if re.fullmatch(r"[A-Za-z0-9_-]{8,80}", candidate):
        return "moodanchor-" + candidate
    return "moodanchor-ip-" + fallback_ip.replace(".", "-").replace(":", "-")


def coze_chat(message, user_id, requested_conversation_id=None, new_conversation=False):
    """Call Coze v3 streaming API and collect assistant deltas into one response."""
    if not COZE_API_KEY or not COZE_BOT_ID:
        raise RuntimeError("Coze is not configured")
    payload = {
        "bot_id": COZE_BOT_ID,
        "user_id": user_id,
        "stream": True,
        "auto_save_history": True,
        "additional_messages": [{"role": "user", "content": message, "content_type": "text"}],
    }
    if new_conversation:
        coze_conversations.pop(user_id, None)
        conversation_id = ""
    else:
        conversation_id = str(requested_conversation_id or coze_conversations.get(user_id) or "").strip()
    endpoint = COZE_API_BASE.rstrip("/") + "/v3/chat"
    # Coze v3 reads the existing conversation identifier from the query string.
    # Sending it only in the JSON body causes a new conversation on every request.
    if conversation_id:
        endpoint += "?" + urllib.parse.urlencode({"conversation_id": conversation_id})
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": "Bearer " + COZE_API_KEY, "Content-Type": "application/json"},
        method="POST",
    )
    event_name, parts = "", []
    with urllib.request.urlopen(request, timeout=90) as response:
        for raw_line in response:
            line = raw_line.decode("utf-8").strip()
            if line.startswith("event:"):
                event_name = line[6:].strip()
            elif line.startswith("data:"):
                data = json.loads(line[5:].strip())
                if event_name == "conversation.chat.created" and data.get("conversation_id"):
                    coze_conversations[user_id] = data["conversation_id"]
                elif event_name == "conversation.message.delta" and data.get("role") == "assistant":
                    parts.append(str(data.get("content", "")))
                elif event_name == "conversation.chat.failed":
                    error = data.get("last_error") or {}
                    raise RuntimeError(error.get("msg") or "Coze chat failed")
    reply = "".join(parts).strip()
    if not reply:
        raise RuntimeError("Coze returned an empty reply")
    return reply, coze_conversations.get(user_id) or conversation_id


if __name__ == "__main__":
    print("MoodAnchor multi-model proxy listening on 0.0.0.0:%d" % PORT, flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
