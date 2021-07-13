# License-Plate-Recognition

## 简介

一个用于在雾条件下进行车牌识别的算法。

去雾部分使用了暗通道先验的去雾算法。

识别部分先使用高斯模糊、滤波、边缘检测、 开操作等图像处理方法提取出车牌部分的字母和数字图，然后使用神经网络对其进行识别。



## 效果

![result](E:\projects\License-Plate-Recognition\images\result.png)



## 环境

Visual Studio 2017

OpenCV 3.4

OpenCV_contrib

