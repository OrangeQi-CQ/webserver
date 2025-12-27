#!/bin/bash
kill -9 $(lsof -t -i:8080 2>/dev/null) 2>/dev/null

# 1. 启动服务器进程（后台）
./server  &
SERVER_PID=$!

# 2. 循环检测端口是否就绪（核心修复）
until nc -z localhost 8080; do
  sleep 0.1
done

# 3. 替换curl为ab并发测试（核心修改）
# 配置参数：可根据需要调整
CONCURRENCY=1000    # 并发数
TOTAL_REQUEST=10000  # 总请求数
echo "开始ab并发测试：并发数=$CONCURRENCY，总请求数=$TOTAL_REQUEST"
ab -c $CONCURRENCY -n $TOTAL_REQUEST http://localhost:8080/ > ab_test_result.txt 2>&1
# 实时输出测试结果（可选，也可查看ab_test_result.txt）
cat ab_test_result.txt

# 4. 等待测试完成（ab是阻塞执行，无需额外wait）
# 5. 终止服务器进程
if ps -p $SERVER_PID >/dev/null; then
  kill -9 $SERVER_PID
  echo -e "\n服务器进程($SERVER_PID)已自动终止"
fi

# 6. 清理文件（保留你的rm server）
rm -f server
echo "测试完成，已清理可执行文件"