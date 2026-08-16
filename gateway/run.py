from pathlib import Path

import uvicorn
from dotenv import load_dotenv

load_dotenv(Path(__file__).resolve().parent / ".env")
from app.config import settings


if __name__ == "__main__":
    uvicorn.run("app.main:app", host=settings.web_host, port=settings.web_port, reload=False)
