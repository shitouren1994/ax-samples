# examples

AX-Samples 将不断更新最流行的、实用的、有趣的示例代码。

- 物体分类
  - MobileNetv1
  - MobileNetv2
  - MobileOne-s0
  - ResNet18
  - ResNet50
- 物体检测
  - [YOLOv3(Paddle)](#yolov3paddle)
  - YOLOv3
  - YOLOv3-Tiny
  - YOLOv4
  - YOLOv4-Tiny
  - YOLOv4-Tiny-3l
  - YOLOv5s
  - YOLOv7-Tiny
  - YOLOX-S
  - YOLO-Fastest-XL
  - NanoDet
- 人型检测
  - YOLO-Fastest-Body
- 障碍物检测 (扫地机场景)
  - Robot-Obstacle-Detect
- 人体关键点
  - HRNet
- 人体分割
  - PP-HumanSeg
- 传统 CV 操作
  - CropResize
- Pipeline 示例
  - NV12 -> CropResize -> NN(Classification)

## 运行示例

下面以物体分类、物体检测 两个常见任务说明如何在 AX620A 开发板上运行相关示例。

### 运行准备

登录 AX620A 开发板，在 `root` 路径下创建 `samples` 文件夹。

- 将 [快速编译](../docs/compile.md) 中编译生成的可执行示例拷贝到 `/root/samples/` 路径下；
- 将 **[ModelZoo](https://pan.baidu.com/s/1zm2M-vqiss4Rmk-uSoGO7w)** (*pwd: euy7*) 中相应的 **joint** 模型 `mobilenetv2.joint` 、 `yolov5s.joint` 拷贝到  `/root/samples/` 路径下；
- 将测试图片拷贝到 `/root/samples` 路径下。

```
/root/samples # ls -l
total 40644
-rwx--x--x    1 root     root       3805332 Mar 22 14:01 ax_classification
-rwx--x--x    1 root     root       3979652 Mar 22 14:01 ax_yolov5s
-rw-------    1 root     root        140391 Mar 22 10:39 cat.jpg
-rw-------    1 root     root        163759 Mar 22 14:01 dog.jpg
-rw-------    1 root     root       4299243 Mar 22 14:00 mobilenetv2.joint
-rw-------    1 root     root      29217004 Mar 22 14:04 yolov5s.joint
```

### 运行示例
- 物体分类：MobileNetv2 

```
/root/samples # ./ax_classification -m mobilenetv2.joint -i cat.jpg -r 10
--------------------------------------
model file : mobilenetv2.joint
image file : cat.jpg
img_h, img_w : 224 224
Run-Joint Runtime version: 0.5.6
--------------------------------------
[INFO]: Virtual npu mode is 1_1

Tools version: 0.6.0.32
8a011dfa
10.6865, 285
10.3324, 283
9.1559, 281
9.1174, 356
9.0098, 282
--------------------------------------
Create handle took 201.32 ms (neu 6.36 ms, axe 0.00 ms, overhead 194.96 ms)
--------------------------------------
Repeat 10 times, avg time 4.20 ms, max_time 4.67 ms, min_time 4.14 ms
```

- 物体检测：YOLOv5s

```
/root/samples # ./ax_yolov5s -m yolov5s.joint -i dog.jpg -r 10
--------------------------------------
model file : yolov5s.joint
image file : dog.jpg
img_h, img_w : 640 640
Run-Joint Runtime version: 0.5.8
--------------------------------------
[INFO]: Virtual npu mode is 1_1

Tools version: 0.6.0.32
8a011dfa
run over: output len 3
post process cost time:5.52 ms
--------------------------------------
Create handle took 396.61 ms (neu 18.13 ms, axe 0.00 ms, overhead 378.48 ms)
--------------------------------------
Repeat 10 times, avg time 26.59 ms, max_time 27.18 ms, min_time 26.51 ms
--------------------------------------
detection num: 3
16:  92%, [ 133,  219,  312,  543], dog
 2:  81%, [ 470,   77,  692,  170], car
 1:  60%, [ 169,  120,  565,  417], bicycle
```

- 物体检测：YOLOX-S
```
/tmp/qtang # ./ax_yoloxs -m yolox_s_cut.joint -i dog.jpg -r 10
--------------------------------------
model file : yolox_s_cut.joint
image file : dog.jpg
img_h, img_w : 640 640
Run-Joint Runtime version: 0.5.8
--------------------------------------
[INFO]: Virtual npu mode is 1_1

Tools version: 0.6.0.32
8a011dfa
run over: output len 3
--------------------------------------
Create handle took 497.16 ms (neu 23.64 ms, axe 0.00 ms, overhead 473.52 ms)
--------------------------------------
Repeat 10 times, avg time 41.65 ms, max_time 42.37 ms, min_time 41.55 ms
--------------------------------------
detection num: 4
 1:  97%, [ 123,  119,  569,  417], bicycle
16:  95%, [ 136,  222,  307,  540], dog
 7:  72%, [ 470,   75,  688,  171], truck
58:  53%, [ 685,  111,  716,  154], potted plant
```

## 模型说明
### YOLOv3(Paddle)
YOLOv3(Paddle) 源自国内产业级深度学习开源框架飞桨的目标检测开发套件 [PaddleDetection](https://github.com/PaddlePaddle/PaddleDetection)，通过速度与精度权衡，我们选择基于 416尺度的 [YOLOv3-Res34](https://github.com/PaddlePaddle/PaddleDetection/tree/develop/configs/yolov3/) 进行功能展示。

#### Paddle2ONNX

- git clone https://github.com/PaddlePaddle/PaddleDetection.git
- 参考 PaddleDetection/deploy/EXPORT_ONNX_MODEL.md 导出 onnx 模型

#### ONNX2Joint

- 目前需通过 FAE 获取AI工具链进行尝试
- 可通过 ModelZoo 中预先转换好的 `yolov3-paddle-416.joint` 进行体验

#### Sample

```
/root/samples # ./ax_paddle_yolov3 -m yolov3-paddle-416.joint -i dog.jpg -r 100
--------------------------------------
model file : yolov3-paddle-416.joint
image file : dog.jpg
img_h, img_w : 416 416
Run-Joint Runtime version: 0.5.6
--------------------------------------
[INFO]: Virtual npu mode is 1_1

Tools version: 0.6.0.22
2ed4ac96
run over: output len 3
YoloDetectionOutput init param[0]
--------------------------------------
Create handle took 1676.07 ms (neu 27.90 ms, axe 0.00 ms, overhead 1648.17 ms)
--------------------------------------
Repeat 100 times, avg time 40.59 ms, max_time 42.25 ms, min_time 40.44 ms
--------------------------------------
detection num: 3
 1:  94%, [ 119,  132,  569,  434], bicycle
 2:  76%, [ 467,   82,  684,  168], car
16:  59%, [ 127,  201,  323,  534], dog
```
