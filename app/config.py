from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT_DIR / "data"
DATA_DIR.mkdir(exist_ok=True)

DB_PATH = DATA_DIR / "enterprise_im.db"
APP_NAME = "AI-Native IM Enterprise"
SESSION_TTL_SECONDS = 60 * 60 * 24
PRESENCE_TTL_SECONDS = 45
