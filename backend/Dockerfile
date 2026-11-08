FROM python:3.10-slim
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg libopus0 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /srv
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app ./app
COPY config.yaml prompt.txt ./
COPY tests ./tests
ENV PYTHONUNBUFFERED=1
EXPOSE 8000
CMD ["python", "-m", "app.main"]
