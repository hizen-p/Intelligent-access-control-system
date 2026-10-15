# 智能家居项目 —— 源码与部署说明

本项目由两个 Qt 工程组成：运行在 **T113 开发板**上的终端程序，以及运行在**虚拟机**中的服务器（上位机）程序。两者通过 TCP / UDP 通信，实现门禁识别（人脸、指纹、密码）、可视对讲、灯光控制与环境监测等功能。

---

## 一、目录结构

```
最终项目/
├── docs/                        说明文档
│   ├── README.md                本文件
│   └── build-and-deploy.txt     编译与部署说明（原「编译和部署必看」两份合并）
│
├── smartclient/                    【终端】T113 下位机源码，可执行文件                   |                                    TARGET=smartclient
│   ├── camera/                  摄像头模块：V4L2 采集 + UDP 推流服务
│   ├── managers/                业务管理器：人脸 / 指纹 / 密码 / 音频
│   ├── pcm/                     提示音资源（.pcm 烧写文件 + .wav 原始文件）
│   ├── pics/                    UI 图片与音效资源
│   ├── HailinFingerprint.*      海凌科指纹模块串口驱动
│   ├── audiosenderthread.*      音频发送线程（UDP）
│   ├── audioreceiverthread.*    音频接收线程（UDP）
│   ├── camera_push_thread.*     摄像头推流线程
│   ├── form_call.*              通话界面
│   ├── form_finger.*            指纹录入界面
│   ├── form_passwd.*            密码输入界面
│   ├── widget.*                 主界面
│   ├── smartclient.pro             Qt 工程文件
│   └── res.qrc                  Qt 资源文件
│
├── smartserver/                 【服务器】上位机源码，运行于虚拟机
│   ├── pic/                     UI 图片资源
│   ├── door.mp3                 开门提示音
│   ├── form_phonecall.*         可视对讲窗口
│   ├── slidehintwidget.*         滑动提示控件
│   ├── widget.*                 主界面
│   ├── smartserver.pro          Qt 工程文件
│   └── res.qrc                  Qt 资源文件
│
└── manual-deploy/               需手动拷贝到 T113 开发板的文件
    ├── thd.tar                  第三方库（解压后放入 /usr/lib）
    ├── pcm.zip                  提示音资源（解压后放入 /tmp/pcm）
    ├── haarcascade_frontalface_default.xml   人脸检测分类器（放入 /root）
    ├── zeng_sim.onnx            人脸 AI 模型（暂未使用）
    └── 中文字体乱码问题.png        中文乱码处理参考截图
```

> 说明：`smartserver/` 与 `manual-deploy/` 内部的目录名保持原有英文命名；顶层三个业务目录已统一改为英文名（原 `smartclientnew` → `smartclient`，原 `需要手动部署` → `manual-deploy`）。
> 改名不影响编译：`.pro`、`.qrc`、`#include` 中均未引用顶层目录名，仅 `*.pro.user` 里残留了旧路径，Qt Creator 会自动重建。

---

## 二、编译与部署

完整步骤见 [build-and-deploy.txt](build-and-deploy.txt)，要点如下：

**1. 修改 IP 配置（必须先做）**

服务器与终端必须能互相 Ping 通，虚拟机网络建议使用**桥接模式**。

| 工程 | 文件 | 行号 | 宏 |
|---|---|---|---|
| smartserver | `smartserver/widget.h` | 16-19 | `SELF_IP`、`DEVICE_IP` |
| smartclient | `smartclient/widget.h` | 59-62 | `SERVER_IP`、`SELF_IP` |

**2. 编译终端**

用 Qt Creator 打开 `smartclient/smartclient.pro`，选择 T113 构建套件编译，将生成的 `smartclient` 拷贝到开发板并 `chmod +x ./smartclient`。

**3. 部署依赖文件**

参见 `manual-deploy/` 目录，各文件的目标路径：

| 文件 | 目标路径 |
|---|---|
| `thd.tar` | 解压后全部放入 `/usr/lib`，执行 `ldconfig` |
| `pcm.zip` | 解压后放入 `/tmp/pcm/`（需先 `mkdir /tmp/pcm`） |
| `haarcascade_frontalface_default.xml` | `/root/` 目录下 |

---

## 三、通信端口

| 协议 | 端口 | 用途 | 方向 |
|---|---|---|---|
| TCP | 8887 | 信令：呼叫、挂断、开门等 | 终端 → 服务器 |
| UDP | 12345 | 音频双向传输 | 双向往返 |
| UDP | 23456 | 视频（可视对讲） | 终端 → 服务器 |
| UDP | 50000 | 摄像头画面推送 | 终端 → 服务器 |

---

## 四、功能模块

**终端（smartclient）**

| 模块 | 文件 | 说明 |
|---|---|---|
| 指纹 | `HailinFingerprint.*`、`managers/fingerprint_manager.*` | 海凌科指纹模块，串口通信，支持录入与识别 |
| 人脸 | `managers/face_manager.*`、`widget.*` | OpenCV 级联检测 + LBPH 识别 |
| 密码 | `managers/password_manager.*`、`form_passwd.*` | 密码校验与管理 |
| 音频 | `managers/audio_manager.*`、`audiosender/receiverthread.*` | ALSA 播放 + UDP 收发 |
| 摄像头 | `camera/v4l2camera.*`、`camera/camera_udp_server.*`、`camera_push_thread.*` | V4L2 采集与推流 |
| GPIO | `widget.h` 中 `GpioPe4MonitorThread` | 监听 PE4 引脚电平，触发相应动作 |

**服务器（smartserver）**

| 模块 | 文件 | 说明 |
|---|---|---|
| 主界面 | `widget.*` | 状态显示、天气、时间、灯光控制 |
| 可视对讲 | `form_phonecall.*` | TCP 信令 + UDP 音视频 |
| 环境控制 | `widget.*`、`QSerialPort` | 通过串口发送 Modbus 指令控制工位灯 / 茶桌灯 / 射灯 |
| 摄像头取帧 | `widget.*` | 从终端 UDP 拉取画面 |

---

## 五、已知问题

以下几处引用与实际文件不一致，编译时可能报错，建议一并修正：

1. **`smartclient/res.qrc` 引用了不存在的图片**
   `pics/mybg002.png`、`pics/smartpg.png`、`pics/deviceback.png` 这三个文件在 `pics/` 目录中不存在，`rcc` 编译资源时会失败。删除对应行，或补齐图片文件。

2. **`smartclient/smartclient.pro:43` 引用了不存在的头文件**
   `HEADERS` 中的 `camera_sender.h` 在工程里不存在（qmake 会给出警告）。

3. **`smartclient/widget.cpp` 硬编码了服务器 IP**
   第 202 行与第 206 行写死为 `"192.168.1.4"`，没有使用 `widget.h` 中的 `SERVER_IP` 宏。改 IP 时这两处不会跟随生效，建议改成 `SERVER_IP`。

4. **文档行号已按当前源码校正**
   原「编译和部署必看.txt」写的是 `widget.h` 第 15 行（实际为 16-19 行），「编译和部署必看1.txt」写的是第 51 行（实际为 59-62 行），合并后的 [build-and-deploy.txt](build-and-deploy.txt) 中已更正。
