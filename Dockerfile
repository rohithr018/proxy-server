# Build Stage
FROM gcc:13 AS builder

WORKDIR /app
COPY . .
RUN make clean && make

# Runtime Stage 
FROM gcc:13

WORKDIR /app

COPY --from=builder /app/bin/server ./server

EXPOSE 8080

CMD ["./server"]
