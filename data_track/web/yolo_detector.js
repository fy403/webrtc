/**
 * YOLO目标检测模块
 * 使用TensorFlow.js和COCO-SSD模型进行实时目标检测
 */

class YOLODetector {
    constructor(videoElement, canvasElement) {
        this.video = videoElement;
        this.canvas = canvasElement;
        this.ctx = canvasElement.getContext('2d');
        this.model = null;
        this.isDetecting = false;
        this.detectionInterval = null;
        this.minConfidence = 0.5; // 最小置信度阈值
        this.targetFPS = 15; // 检测帧率(平衡性能和精度)
        
        // COCO数据集的80个类别
        this.classNames = [
            'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus', 'train', 'truck', 'boat',
            'traffic light', 'fire hydrant', 'stop sign', 'parking meter', 'bench', 'bird', 'cat',
            'dog', 'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra', 'giraffe', 'backpack',
            'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee', 'skis', 'snowboard', 'sports ball',
            'kite', 'baseball bat', 'baseball glove', 'skateboard', 'surfboard', 'tennis racket',
            'bottle', 'wine glass', 'cup', 'fork', 'knife', 'spoon', 'bowl', 'banana', 'apple',
            'sandwich', 'orange', 'broccoli', 'carrot', 'hot dog', 'pizza', 'donut', 'cake',
            'chair', 'couch', 'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop',
            'mouse', 'remote', 'keyboard', 'cell phone', 'microwave', 'oven', 'toaster', 'sink',
            'refrigerator', 'book', 'clock', 'vase', 'scissors', 'teddy bear', 'hair drier',
            'toothbrush'
        ];
        
        // 不同类别的颜色映射
        this.classColors = this.generateColorMap();
    }

    /**
     * 生成颜色映射
     */
    generateColorMap() {
        const colors = {};
        const hueStep = 360 / this.classNames.length;
        
        this.classNames.forEach((className, index) => {
            const hue = (index * hueStep) % 360;
            colors[className] = `hsl(${hue}, 70%, 50%)`;
        });
        
        return colors;
    }

    /**
     * 加载模型
     */
    async loadModel() {
        try {
            console.log('正在加载COCO-SSD模型...');

            // 尝试从本地加载TensorFlow.js和COCO-SSD
            if (typeof tf === 'undefined') {
                // 优先尝试加载本地文件
                const localTfLoaded = await this.loadLocalScript('tf.min.js');
                const localCocoSsdLoaded = await this.loadLocalScript('coco-ssd.min.js');

                if (!localTfLoaded || !localCocoSsdLoaded) {
                    console.warn('本地文件未找到,尝试从CDN加载...');
                    // 本地文件不存在时,从CDN加载
                    await this.loadScript('https://cdn.jsdelivr.net/npm/@tensorflow/tfjs@4.17.0/dist/tf.min.js');
                    await this.loadScript('https://cdn.jsdelivr.net/npm/@tensorflow-models/coco-ssd@2.2.3/dist/coco-ssd.min.js');
                }
            }

            // 加载模型
            this.model = await cocoSsd.load();
            console.log('COCO-SSD模型加载成功');

            return true;
        } catch (error) {
            console.error('加载模型失败:', error);
            throw error;
        }
    }

    /**
     * 动态加载脚本
     */
    loadScript(src) {
        return new Promise((resolve, reject) => {
            // 检查脚本是否已加载
            const scriptId = src.split('/').pop();
            if (document.getElementById(scriptId)) {
                resolve();
                return;
            }

            const script = document.createElement('script');
            script.id = scriptId;
            script.src = src;
            script.onload = resolve;
            script.onerror = reject;
            document.head.appendChild(script);
        });
    }

    /**
     * 加载本地脚本
     */
    loadLocalScript(filename) {
        return new Promise((resolve, reject) => {
            const scriptId = filename;
            if (document.getElementById(scriptId)) {
                resolve(true);
                return;
            }

            const script = document.createElement('script');
            script.id = scriptId;
            script.src = filename; // 本地相对路径

            script.onload = () => {
                console.log(`本地文件 ${filename} 加载成功`);
                resolve(true);
            };

            script.onerror = () => {
                console.warn(`本地文件 ${filename} 未找到`);
                resolve(false);
            };

            document.head.appendChild(script);
        });
    }

    /**
     * 开始检测
     */
    async startDetection() {
        if (!this.model) {
            console.warn('模型未加载,请先调用loadModel()');
            return;
        }
        
        if (this.isDetecting) {
            console.warn('检测已在进行中');
            return;
        }
        
        this.isDetecting = true;
        console.log('开始YOLO检测');
        
        // 调整画布大小以匹配视频
        this.resizeCanvas();
        
        // 开始检测循环
        const intervalMs = 1000 / this.targetFPS;
        this.detectionInterval = setInterval(() => {
            this.detect();
        }, intervalMs);
    }

    /**
     * 停止检测
     */
    stopDetection() {
        if (!this.isDetecting) {
            return;
        }
        
        this.isDetecting = false;
        
        if (this.detectionInterval) {
            clearInterval(this.detectionInterval);
            this.detectionInterval = null;
        }
        
        // 清除画布
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        console.log('YOLO检测已停止');
    }

    /**
     * 调整画布大小
     */
    resizeCanvas() {
        if (!this.video) return;
        
        const videoRect = this.video.getBoundingClientRect();
        
        // 设置画布尺寸匹配视频显示尺寸
        this.canvas.width = videoRect.width;
        this.canvas.height = videoRect.height;
        
        // 设置画布位置与视频重合
        this.canvas.style.position = 'absolute';
        this.canvas.style.top = '0';
        this.canvas.style.left = '0';
        this.canvas.style.pointerEvents = 'none'; // 让鼠标事件穿透
    }

    /**
     * 执行检测
     */
    async detect() {
        if (!this.video || !this.model || this.video.readyState !== 4) {
            return;
        }
        
        try {
            // 执行检测
            const predictions = await this.model.detect(this.video);
            
            // 过滤低置信度的检测结果
            const filteredPredictions = predictions.filter(
                prediction => prediction.score >= this.minConfidence
            );
            
            // 绘制检测框
            this.drawPredictions(filteredPredictions);
            
            // 可以通过回调函数传递检测结果
            if (this.onDetection) {
                this.onDetection(filteredPredictions);
            }
            
        } catch (error) {
            console.error('检测错误:', error);
        }
    }

    /**
     * 绘制检测结果
     */
    drawPredictions(predictions) {
        // 清除画布
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        // 计算视频实际尺寸与显示尺寸的比例
        const scaleX = this.canvas.width / this.video.videoWidth;
        const scaleY = this.canvas.height / this.video.videoHeight;
        
        predictions.forEach(prediction => {
            const [x, y, width, height] = prediction.bbox;
            
            // 转换坐标到画布坐标系
            const canvasX = x * scaleX;
            const canvasY = y * scaleY;
            const canvasWidth = width * scaleX;
            const canvasHeight = height * scaleY;
            
            // 获取类别和颜色
            const className = prediction.class;
            const confidence = prediction.score;
            const color = this.classColors[className] || '#00FF00';
            
            // 绘制边框
            this.ctx.strokeStyle = color;
            this.ctx.lineWidth = 3;
            this.ctx.strokeRect(canvasX, canvasY, canvasWidth, canvasHeight);
            
            // 使用rgba设置半透明填充 (0.15 = 15%透明度)
            this.ctx.globalAlpha = 0.15;
            this.ctx.fillStyle = color;
            this.ctx.fillRect(canvasX, canvasY, canvasWidth, canvasHeight);
            this.ctx.globalAlpha = 1.0; // 恢复不透明度
            
            // 绘制标签背景
            const labelText = `${className} ${(confidence * 100).toFixed(1)}%`;
            this.ctx.font = 'bold 14px Arial';
            const textMetrics = this.ctx.measureText(labelText);
            const textHeight = 20;
            const textPadding = 6;
            
            // 标签背景使用不透明
            this.ctx.globalAlpha = 1.0;
            this.ctx.fillStyle = color;
            this.ctx.fillRect(
                canvasX,
                canvasY - textHeight,
                textMetrics.width + textPadding * 2,
                textHeight
            );
            
            // 绘制标签文字
            this.ctx.fillStyle = '#FFFFFF';
            this.ctx.textBaseline = 'middle';
            this.ctx.fillText(
                labelText,
                canvasX + textPadding,
                canvasY - textHeight / 2
            );
        });
    }

    /**
     * 设置最小置信度
     */
    setMinConfidence(confidence) {
        this.minConfidence = Math.max(0, Math.min(1, confidence));
    }

    /**
     * 设置检测帧率
     */
    setTargetFPS(fps) {
        this.targetFPS = Math.max(1, Math.min(60, fps));
        
        // 如果正在检测,重启检测循环
        if (this.isDetecting) {
            this.stopDetection();
            this.startDetection();
        }
    }

    /**
     * 设置检测回调函数
     */
    onDetectionCallback(callback) {
        this.onDetection = callback;
    }

    /**
     * 获取检测状态
     */
    isRunning() {
        return this.isDetecting;
    }

    /**
     * 销毁检测器
     */
    dispose() {
        this.stopDetection();
        
        if (this.model) {
            this.model = null;
        }
        
        console.log('YOLO检测器已销毁');
    }
}

// 导出为全局变量
window.YOLODetector = YOLODetector;
