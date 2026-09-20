FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        python3 gcc clang libclang-rt-18-dev libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["python3", "tests/test_regressions.py", "-v"]
