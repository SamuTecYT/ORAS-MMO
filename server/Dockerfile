# ORAS MMO relay server — imagen mínima (solo la biblioteca estándar de Python)
FROM python:3.12-slim

WORKDIR /app
COPY server.py .

# El ranking (ranking_<sala>.json) se guarda aquí: móntalo como volumen para no perderlo al actualizar
ENV ORAS_RANK_DIR=/data PYTHONUNBUFFERED=1
VOLUME ["/data"]

# Sin privilegios de root dentro del contenedor
RUN useradd --create-home --uid 10001 oras && mkdir -p /data && chown oras /data
USER oras

EXPOSE 9000/tcp
CMD ["python", "server.py"]
