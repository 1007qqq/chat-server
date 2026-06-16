import asyncio
import json
from collections import defaultdict


class EventHub:
    def __init__(self):
        self._subscribers: dict[int, list[asyncio.Queue[str]]] = defaultdict(list)
        self._lock = asyncio.Lock()

    async def subscribe(self, user_id: int) -> asyncio.Queue[str]:
        queue: asyncio.Queue[str] = asyncio.Queue(maxsize=100)
        async with self._lock:
            self._subscribers[user_id].append(queue)
        return queue

    async def unsubscribe(self, user_id: int, queue: asyncio.Queue[str]):
        async with self._lock:
            queues = self._subscribers.get(user_id, [])
            if queue in queues:
                queues.remove(queue)
            if not queues:
                self._subscribers.pop(user_id, None)

    async def publish(self, user_ids: list[int] | set[int], event: dict):
        payload = json.dumps(event, ensure_ascii=False, separators=(",", ":"))
        async with self._lock:
            queues = [q for uid in set(user_ids) for q in self._subscribers.get(int(uid), [])]
        for queue in queues:
            try:
                queue.put_nowait(payload)
            except asyncio.QueueFull:
                pass

    async def online_user_ids(self) -> set[int]:
        async with self._lock:
            return {uid for uid, queues in self._subscribers.items() if queues}


event_hub = EventHub()
