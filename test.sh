#!/bin/bash
# 极简脚本：确保服务器就绪后再执行测试，避免连接拒绝
g++ main.cpp -o server || exit 1
kill -9 $(lsof -t -i:8080 2>/dev/null) 2>/dev/null

# 1. 启动服务器进程（后台）
./server >/dev/null 2>&1 &
SERVER_PID=$!

# 2. 循环检测端口是否就绪（核心修复）
until nc -z localhost 8080; do
  sleep 0.1
done

# 3. 启动测试进程（独立进程）
(curl http://localhost:8080) &
TEST_PID=$!

# 可选：等待测试完成，输出结果
wait $TEST_PID


if ps -p $SERVER_PID >/dev/null; then
  kill -9 $SERVER_PID
  echo -e "\n服务器进程($SERVER_PID)已自动终止"
fi

rm server