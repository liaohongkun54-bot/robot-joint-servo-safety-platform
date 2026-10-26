# 合成测试数据

本目录的 synthetic-wire-vectors.json 与 synthetic-feedback.jsonl 由 generate_synthetic_fixtures.py 从常量生成，不读任何真实日志、不打开串口。
UID 0x11111111/0x22222222/0x33333333、时间、样本、请求号和会话标识均为合成值。697 ms 是故障分类回归用例的固定输入，不是公开的实测结果。
operator-vectors.json 和内存端点同样仅为软件测试。不得将这些夹具或曲线当作硬件验收数据。
运行 python PC/NativeQt/tests/generate_synthetic_fixtures.py 可重新生成；C++常规构建无需Python。
