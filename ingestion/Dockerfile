FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY miniair_ingest miniair_ingest

RUN useradd --system --no-create-home miniair
USER miniair

ENTRYPOINT ["python", "-m", "miniair_ingest"]
