FROM python:3.12-slim

COPY --from=ghcr.io/astral-sh/uv:latest /uv /usr/local/bin/uv

WORKDIR /deps

COPY pyproject.toml uv.lock ./

RUN uv export --frozen --group docs --no-hashes --no-emit-project -o requirements.txt \
    && pip install --no-cache-dir -r requirements.txt
