/**
 * IMU Trajectory Calculator & 2D View Visualizer
 * 从加速度计和姿态数据计算运动轨迹，支持三视图切换
 */
class IMUTrajectory {
    constructor() {
        // ====== 积分状态 ======
        this.position = { x: 0, y: 0, z: 0 };
        this.velocity = { x: 0, y: 0, z: 0 };
        this.lastAccel = { x: 0, y: 0, z: 0 };
        this.lastTimestamp = null;
        this.attitude = { pitch: 0, roll: 0, yaw: 0 };

        this.trajectory = [];
        this.maxTrajectoryPoints = 2000;
        this.minPositionDelta = 0.003;

        this.accelScale = 9.81;        // 加速度单位换算：1.0 表示输入已是 m/s²；9.81 表示输入为 g
        this.subtractGravity = false;  // 若 JSON 中的 accel_* 已是线性加速度（去重力），设为 false
        this.accelThreshold = 0.01;    // 阈值，单位 m/s²
        this.velDamping = 0.998;       // 速度衰减（漂移补偿）
        this.gravity = 9.81;

        // ====== 可视化 ======
        this.canvas = null;
        this.ctx = null;
        this.W = 520;
        this.H = 520;
        this.panX = 0;          // 当 followCurrent=false 时是世界坐标；为 true 时是相对偏移
        this.panY = 0;
        this.followCurrent = true;  // 视图中心是否跟随当前位置
        this.zoom = 80;        // 像素/米，默认大一些让静止轨迹可见
        this.isDragging = false;
        this.lastMX = 0;
        this.lastMY = 0;
        this.panelVisible = false;
        this.animationId = null;

        // 'top'=俯视(X-Y)  'front'=正视(X-Z)  'side'=侧视(Y-Z)  'all'=三视图
        this.viewMode = 'all';

        this.colors = {
            bg: '#080818',
            subBg: 'rgba(8,8,24,0.92)',
            grid: 'rgba(0,180,100,0.10)',
            gridStrong: 'rgba(0,180,100,0.22)',
            axisX: '#ff5555',
            axisY: '#55ff55',
            axisZ: '#5588ff',
            traj: '#00ff88',
            trajGlow: 'rgba(0,255,136,0.30)',
            curPos: '#ff8800',
            text: '#00cc66',
            textDim: 'rgba(0,204,102,0.5)',
            border: 'rgba(0,204,102,0.2)',
        };
    }

    // ==================== 核心算法 ====================

    update(accelX, accelY, accelZ, pitch, roll, yaw, timestamp) {
        accelX = (parseFloat(accelX) || 0) * this.accelScale;
        accelY = (parseFloat(accelY) || 0) * this.accelScale;
        accelZ = (parseFloat(accelZ) || 0) * this.accelScale;
        pitch = parseFloat(pitch) || 0;
        roll = parseFloat(roll) || 0;
        yaw = parseFloat(yaw) || 0;

        this.attitude = { pitch, roll, yaw };

        // 阈值过滤（单位 m/s²）
        if (Math.abs(accelX) < this.accelThreshold) accelX = 0;
        if (Math.abs(accelY) < this.accelThreshold) accelY = 0;
        if (Math.abs(accelZ) < this.accelThreshold) accelZ = 0;

        const now = performance.now();
        if (this.lastTimestamp === null) {
            this.lastTimestamp = now;
            this.lastAccel = { x: accelX, y: accelY, z: accelZ };
            return;
        }

        let dt = (now - this.lastTimestamp) / 1000;
        if (dt <= 0 || dt > 0.15) dt = 0.02;
        this.lastTimestamp = now;

        const worldAccel = this._rotateToWorld(accelX, accelY, accelZ, roll, pitch, yaw);

        // 如果 JSON 中的 accel 仍包含重力，开启此项
        if (this.subtractGravity) {
            worldAccel.z -= this.gravity;
        }

        // 低通滤波平滑加速度
        const filtered = {
            x: this.accelLPF * this.lastAccel.x + (1 - this.accelLPF) * worldAccel.x,
            y: this.accelLPF * this.lastAccel.y + (1 - this.accelLPF) * worldAccel.y,
            z: this.accelLPF * this.lastAccel.z + (1 - this.accelLPF) * worldAccel.z,
        };

        // 梯形积分：加速度 → 速度
        this.velocity.x += 0.5 * (this.lastAccel.x + filtered.x) * dt;
        this.velocity.y += 0.5 * (this.lastAccel.y + filtered.y) * dt;
        this.velocity.z += 0.5 * (this.lastAccel.z + filtered.z) * dt;

        // 速度衰减（漂移补偿）
        this.velocity.x *= this.velDamping;
        this.velocity.y *= this.velDamping;
        this.velocity.z *= this.velDamping;

        // 零速更新：加速度接近零时加大阻尼（静止阈值约 0.15 m/s²）
        const mag = Math.sqrt(filtered.x**2 + filtered.y**2 + filtered.z**2);
        if (mag < 0.15) {
            this.velocity.x *= 0.85;
            this.velocity.y *= 0.85;
            this.velocity.z *= 0.85;
        }

        // 积分：速度 → 位置
        this.position.x += this.velocity.x * dt;
        this.position.y += this.velocity.y * dt;
        this.position.z += this.velocity.z * dt;

        this.lastAccel = { x: filtered.x, y: filtered.y, z: filtered.z };

        const lp = this.trajectory[this.trajectory.length - 1];
        if (!lp ||
            Math.abs(this.position.x - lp.x) > this.minPositionDelta ||
            Math.abs(this.position.y - lp.y) > this.minPositionDelta ||
            Math.abs(this.position.z - lp.z) > this.minPositionDelta) {
            this.trajectory.push({
                x: this.position.x, y: this.position.y, z: this.position.z, t: timestamp
            });
        }
        if (this.trajectory.length > this.maxTrajectoryPoints) {
            this.trajectory = this.trajectory.slice(-this.maxTrajectoryPoints);
        }
        if (this.panelVisible) this.draw();
    }

    _rotateToWorld(ax, ay, az, rollDeg, pitchDeg, yawDeg) {
        const r = rollDeg  * Math.PI / 180, cr = Math.cos(r), sr = Math.sin(r);
        const p = pitchDeg * Math.PI / 180, cp = Math.cos(p), sp = Math.sin(p);
        const y = yawDeg   * Math.PI / 180, cy = Math.cos(y), sy = Math.sin(y);

        let x = ax, yy = cr*ay - sr*az, z = sr*ay + cr*az;           // roll (X)
        let x2 = cp*x + sp*z, z2 = -sp*x + cp*z;                      // pitch (Y)
        let x3 = cy*x2 - sy*yy, y3 = sy*x2 + cy*yy;                   // yaw (Z)
        return { x: x3, y: y3, z: z2 };
    }

    reset() {
        this.position = { x: 0, y: 0, z: 0 };
        this.velocity = { x: 0, y: 0, z: 0 };
        this.trajectory = [];
        this.lastTimestamp = null;
        this.lastAccel = { x: 0, y: 0, z: 0 };
        // 跟随当前位置开关
        if (this.followCurrent) {
            this.panX = 0;
            this.panY = 0;
        }

        if (this.panelVisible) this.draw();
    }

    // ==================== 2D 投影（视图） ====================

    // 返回 {sx, sy} — 画布像素坐标
    // view: 'top'|'front'|'side'
    _proj(view, wx, wy, wz) {
        const z = this.zoom;
        const px = this.panX, py = this.panY;
        let h, v;
        switch (view) {
            case 'top':   h =  wx; v =  wy; break;  // X→右, Y→上
            case 'front': h =  wx; v =  wz; break;  // X→右, Z→上
            case 'side':  h =  wy; v =  wz; break;  // Y→右, Z→上
            default:      h =  wx; v =  wy; break;
        }
        return { sx: (h - px) * z, sy: -(v - py) * z };  // 注意 sy 翻转（画布Y向下）
    }

    // 返回某个视图的中心世界坐标
    // 当 followCurrent=true 时，中心是当前位置 + panX/panY 偏移；
    // 否则 panX/panY 就是世界坐标中心。
    _getViewCenter(view) {
        const ox = this.followCurrent ? this.position.x : 0;
        const oy = this.followCurrent ? this.position.y : 0;
        const oz = this.followCurrent ? this.position.z : 0;
        switch (view) {
            case 'top':   return { x: ox + this.panX, y: oy + this.panY };
            case 'front': return { x: ox + this.panX, y: oz + this.panY };
            case 'side':  return { x: oy + this.panX, y: oz + this.panY };
            default:      return { x: this.panX, y: this.panY };
        }
    }

    // 获取某个视图的可见范围（世界坐标）
    _getViewBounds(view) {
        const z = this.zoom;
        switch (view) {
            case 'top':   return { hRange: [-this.panX - 2, this.W/z - this.panX + 2], vRange: [this.H/z + this.panY + 2, this.panY - 2] };
            default:      return { hRange: [-2, 2], vRange: [-2, 2] };
        }
    }

    // ==================== 绘制入口 ====================

    draw() {
        if (!this.ctx || !this.canvas) return;
        const ctx = this.ctx;
        ctx.fillStyle = this.colors.bg;
        ctx.fillRect(0, 0, this.W, this.H);

        if (this.viewMode === 'all') {
            this._drawAllViews();
        } else {
            this._drawSingleView(this.viewMode, 0, 0, this.W, this.H);
        }
    }

    // ==================== 单视图 ====================

    _drawSingleView(view, ox, oy, w, h) {
        this._drawSubView(view, ox, oy, w, h, true);
    }

    // ==================== 三视图组合 ====================

    _drawAllViews() {
        const ctx = this.ctx;
        const W = this.W, H = this.H;

        // 布局: 2列 x 2行
        // [0,0]=俯视  [0,1]=姿态信息+图例
        // [1,0]=正视  [1,1]=侧视
        const cx = W / 2, cy = H / 2;
        const margin = 4;

        // 俯视图 左上
        this._drawSubView('top',   margin,       margin,       cx - margin*2, cy - margin*2, false);

        // 姿态面板 右上
        this._drawAttitudePanel(cx + margin, margin, cx - margin*2, cy - margin*2);

        // 正视图 左下
        this._drawSubView('front', margin,       cy + margin,  cx - margin*2, (H - cy) - margin*2, false);

        // 侧视图 右下
        this._drawSubView('side',  cx + margin,  cy + margin,  cx - margin*2, (H - cy) - margin*2, false);

        // 分隔线
        ctx.strokeStyle = this.colors.border; ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, cy); ctx.lineTo(W, cy);
        ctx.moveTo(cx, 0); ctx.lineTo(cx, H);
        ctx.stroke();

        // 底部提示
        ctx.fillStyle = this.colors.textDim;
        ctx.font = '9px "Courier New", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('Drag to pan | Scroll to zoom | Click views to switch full', W/2, H - 6);
    }

    // ==================== 子视图绘制 ====================

    _drawSubView(view, ox, oy, w, h, isSingle) {
        const ctx = this.ctx;

        // 裁剪区域
        ctx.save();
        ctx.beginPath();
        ctx.rect(ox, oy, w, h);
        ctx.clip();

        // 背景
        ctx.fillStyle = this.colors.subBg;
        ctx.fillRect(ox, oy, w, h);

        // 坐标变换：把原点放在当前position + pan在区域的中心
        // 子视图中心对应世界坐标 (panX, panY)
        const cx = ox + w/2, cy = oy + h/2;

        // 确定水平/垂直轴映射
        const center = this._getViewCenter(view);
        let hAxis, vAxis, hLabel, vLabel, axisHColor, axisVColor;
        switch (view) {
            case 'top':
                hAxis = (wx) => (wx - center.x) * this.zoom;
                vAxis = (wy) => -(wy - center.y) * this.zoom;
                hLabel = 'X →'; vLabel = 'Y ↑'; axisHColor = this.colors.axisX; axisVColor = this.colors.axisY;
                break;
            case 'front':
                hAxis = (wx) => (wx - center.x) * this.zoom;
                vAxis = (wz) => -(wz - center.y) * this.zoom;
                hLabel = 'X →'; vLabel = 'Z ↑'; axisHColor = this.colors.axisX; axisVColor = this.colors.axisZ;
                break;
            case 'side':
                hAxis = (wy) => (wy - center.x) * this.zoom;
                vAxis = (wz) => -(wz - center.y) * this.zoom;
                hLabel = 'Y →'; vLabel = 'Z ↑'; axisHColor = this.colors.axisY; axisVColor = this.colors.axisZ;
                break;
        }

        const translate = (wx, wy) => ({ sx: cx + hAxis(wx), sy: cy + vAxis(wy) });

        // --- 网格 ---
        this._drawSubGrid(view, cx, cy, hAxis, vAxis, ox, oy, w, h);

        // --- 轨迹 ---
        this._drawSubTrajectory(view, cx, cy, hAxis, vAxis, ox, oy, w, h);

        // --- 原点十字 ---
        const orig = translate(0, 0);
        ctx.strokeStyle = this.colors.gridStrong; ctx.lineWidth = 1;
        // 水平轴
        ctx.beginPath();
        ctx.moveTo(Math.max(ox, cx + hAxis(-50)), cy);
        ctx.lineTo(Math.min(ox+w, cx + hAxis(50)), cy);
        ctx.stroke();
        // 垂直轴
        ctx.beginPath();
        ctx.moveTo(cx, Math.max(oy, cy + vAxis(-50)));
        ctx.lineTo(cx, Math.min(oy+h, cy + vAxis(50)));
        ctx.stroke();

        // --- 当前位置 ---
        const pp = translate(this.position.x, view === 'side' ? this.position.y : (view === 'front' ? this.position.z : this.position.y));
        if (pp.sx >= ox && pp.sx <= ox+w && pp.sy >= oy && pp.sy <= oy+h) {
            ctx.beginPath(); ctx.arc(pp.sx, pp.sy, 7, 0, Math.PI*2);
            ctx.fillStyle = 'rgba(255,136,0,0.2)'; ctx.fill();
            ctx.beginPath(); ctx.arc(pp.sx, pp.sy, 4, 0, Math.PI*2);
            ctx.fillStyle = this.colors.curPos; ctx.fill();
            ctx.strokeStyle = this.colors.curPos; ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(pp.sx-10, pp.sy); ctx.lineTo(pp.sx+10, pp.sy);
            ctx.moveTo(pp.sx, pp.sy-10); ctx.lineTo(pp.sx, pp.sy+10);
            ctx.stroke();
        }

        // --- 标题 ---
        let title;
        switch (view) {
            case 'top':   title = 'TOP (俯视)  X-Y'; break;
            case 'front': title = 'FRONT (正视) X-Z'; break;
            case 'side':  title = 'SIDE (侧视)  Y-Z'; break;
        }
        ctx.fillStyle = this.colors.text;
        ctx.font = 'bold 10px "Courier New", monospace';
        ctx.textAlign = 'left';
        ctx.fillText(title, ox + 6, oy + 14);

        // 轴标签
        ctx.fillStyle = axisHColor;
        ctx.textAlign = 'right';
        if (isSingle) {
            ctx.font = 'bold 12px monospace';
            ctx.fillText(hLabel, ox + w - 8, cy + 14);
            ctx.fillStyle = axisVColor;
            ctx.fillText(vLabel, cx + 14, oy + 18);
        }

        // 比例尺
        const scaleM = 1;  // 显示1m比例尺
        const scalePx = scaleM * this.zoom;
        if (scalePx > 20) {
            const sx = ox + w - 10 - scalePx, sy = oy + h - 20;
            ctx.strokeStyle = this.colors.textDim; ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(sx, sy); ctx.lineTo(sx + scalePx, sy); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(sx, sy-3); ctx.lineTo(sx, sy+3); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(sx+scalePx, sy-3); ctx.lineTo(sx+scalePx, sy+3); ctx.stroke();
            ctx.fillStyle = this.colors.textDim;
            ctx.font = '9px monospace'; ctx.textAlign = 'center';
            ctx.fillText(`${scaleM}m`, sx + scalePx/2, sy - 5);
        }

        ctx.restore();
    }

    _drawSubGrid(view, cx, cy, hAxis, vAxis, ox, oy, w, h) {
        const ctx = this.ctx;

        // 根据 zoom 选网格步长
        let step;
        if (this.zoom > 80) step = 0.1;
        else if (this.zoom > 40) step = 0.2;
        else if (this.zoom > 20) step = 0.5;
        else if (this.zoom > 10) step = 1;
        else step = 2;

        const range = 50;

        ctx.strokeStyle = this.colors.grid;
        ctx.lineWidth = 0.5;

        // 水平网格线（沿垂直轴）
        for (let v = -range; v <= range; v += step) {
            if (Math.abs(v) < 0.001) continue;
            const sy = cy + vAxis(v);
            if (sy < oy || sy > oy + h) continue;
            ctx.beginPath();
            ctx.moveTo(ox, sy);
            ctx.lineTo(ox + w, sy);
            ctx.stroke();
        }

        // 垂直网格线（沿水平轴）
        for (let hv = -range; hv <= range; hv += step) {
            if (Math.abs(hv) < 0.001) continue;
            const sx = cx + hAxis(hv);
            if (sx < ox || sx > ox + w) continue;
            ctx.beginPath();
            ctx.moveTo(sx, oy);
            ctx.lineTo(sx, oy + h);
            ctx.stroke();
        }
    }

    _drawSubTrajectory(view, cx, cy, hAxis, vAxis, ox, oy, w, h) {
        const pts = this.trajectory;
        if (pts.length < 2) return;
        const ctx = this.ctx;

        const getVal = (pt, key) => {
            switch (view) {
                case 'top':   return key === 'h' ? pt.x : pt.y;
                case 'front': return key === 'h' ? pt.x : pt.z;
                case 'side':  return key === 'h' ? pt.y : pt.z;
            }
        };

        // 光晕
        ctx.strokeStyle = this.colors.trajGlow;
        ctx.lineWidth = 4;
        ctx.beginPath();
        let first = true, visible = false;
        for (let i = 0; i < pts.length; i++) {
            const sx = cx + hAxis(getVal(pts[i], 'h'));
            const sy = cy + vAxis(getVal(pts[i], 'v'));
            if (sx >= ox-5 && sx <= ox+w+5 && sy >= oy-5 && sy <= oy+h+5) {
                if (first) { ctx.moveTo(sx, sy); first = false; }
                else ctx.lineTo(sx, sy);
                visible = true;
            } else {
                if (!first && visible) { first = true; }
            }
        }
        ctx.stroke();

        // 主线
        ctx.strokeStyle = this.colors.traj;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        first = true;
        for (let i = 0; i < pts.length; i++) {
            const sx = cx + hAxis(getVal(pts[i], 'h'));
            const sy = cy + vAxis(getVal(pts[i], 'v'));
            if (sx >= ox-5 && sx <= ox+w+5 && sy >= oy-5 && sy <= oy+h+5) {
                if (first) { ctx.moveTo(sx, sy); first = false; }
                else ctx.lineTo(sx, sy);
            } else {
                if (!first) { first = true; }
            }
        }
        ctx.stroke();
    }

    // ==================== 姿态面板 ====================

    _drawAttitudePanel(ox, oy, w, h) {
        const ctx = this.ctx;
        ctx.save();
        ctx.beginPath();
        ctx.rect(ox, oy, w, h);
        ctx.clip();

        ctx.fillStyle = this.colors.subBg;
        ctx.fillRect(ox, oy, w, h);

        const cx = ox + w/2, cy = oy + h/2;
        const { pitch, roll, yaw } = this.attitude;
        const p = this.position, v = this.velocity;

        // 标题
        ctx.fillStyle = this.colors.text;
        ctx.font = 'bold 10px "Courier New", monospace';
        ctx.textAlign = 'left';
        ctx.fillText('ATTITUDE (姿态)', ox + 6, oy + 14);

        // ---- 人工地平线 ----
        const horizonR = Math.min(w, h) * 0.28;
        const hcx = cx, hcy = oy + h * 0.38;

        // 圆形边框
        ctx.strokeStyle = this.colors.border; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(hcx, hcy, horizonR, 0, Math.PI*2); ctx.stroke();

        // 裁剪到圆内
        ctx.save();
        ctx.beginPath(); ctx.arc(hcx, hcy, horizonR, 0, Math.PI*2); ctx.clip();

        // 天空/地面（根据roll旋转）
        const rollRad = roll * Math.PI / 180;
        const pitchOffset = (pitch / 90) * horizonR;

        ctx.save();
        ctx.translate(hcx, hcy);
        ctx.rotate(-rollRad); // roll逆时针为使地平线反应正确

        // 天空（上半）
        ctx.fillStyle = '#1a3355';
        ctx.fillRect(-horizonR, -horizonR - pitchOffset, horizonR*2, horizonR + pitchOffset);
        // 地面（下半）
        ctx.fillStyle = '#4a3020';
        ctx.fillRect(-horizonR, -pitchOffset, horizonR*2, horizonR + pitchOffset);

        // 地平线
        ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(-horizonR, -pitchOffset);
        ctx.lineTo(horizonR, -pitchOffset);
        ctx.stroke();

        // 短刻度线
        for (let d = -30; d <= 30; d += 10) {
            const dy = -pitchOffset - (d / 90) * horizonR;
            ctx.strokeStyle = 'rgba(255,255,255,0.4)'; ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(-8, dy); ctx.lineTo(8, dy); ctx.stroke();
            if (d % 20 === 0) {
                ctx.fillStyle = 'rgba(255,255,255,0.6)'; ctx.font = '7px monospace'; ctx.textAlign = 'center';
                ctx.fillText(String(d), 0, dy - 2);
            }
        }

        ctx.restore(); // undo rotate

        // Roll 指针（三角形）
        ctx.fillStyle = '#ff4444';
        ctx.beginPath();
        ctx.moveTo(hcx, hcy - horizonR + 4);
        ctx.lineTo(hcx - 5, hcy - horizonR - 4);
        ctx.lineTo(hcx + 5, hcy - horizonR - 4);
        ctx.closePath(); ctx.fill();

        ctx.restore(); // undo circle clip

        // ---- 罗盘 (Yaw) ----
        const compassR = Math.min(w, h) * 0.22;
        const compY = oy + h - compassR - 12;

        ctx.strokeStyle = this.colors.border; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.arc(cx, compY, compassR, 0, Math.PI*2); ctx.stroke();

        // NESW 标记
        const dirs = [
            { label: 'N', angle: 0 },
            { label: 'E', angle: 90 },
            { label: 'S', angle: 180 },
            { label: 'W', angle: 270 },
        ];
        const yawRad = yaw * Math.PI / 180;
        dirs.forEach(d => {
            const a = (d.angle - yaw) * Math.PI / 180;
            const lx = cx + Math.sin(a) * compassR * 0.82;
            const ly = compY - Math.cos(a) * compassR * 0.82;
            ctx.fillStyle = d.label === 'N' ? '#ff4444' : this.colors.textDim;
            ctx.font = 'bold 9px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(d.label, lx, ly + 3);
        });

        // 航向箭头
        ctx.fillStyle = '#00ff88';
        ctx.beginPath();
        const arrowTip = compY - compassR + 3;
        ctx.moveTo(cx, arrowTip);
        ctx.lineTo(cx - 4, arrowTip + 8);
        ctx.lineTo(cx + 4, arrowTip + 8);
        ctx.closePath(); ctx.fill();

        // ---- 姿态数值 ----
        const infoX = ox + w - 10;
        let iy = oy + 30;
        const items = [
            { label: 'Roll',  val: roll.toFixed(1) + '°',  color: '#ff8888' },
            { label: 'Pitch', val: pitch.toFixed(1) + '°', color: '#88ff88' },
            { label: 'Yaw',   val: yaw.toFixed(1) + '°',   color: '#8888ff' },
        ];
        items.forEach(item => {
            ctx.fillStyle = item.color;
            ctx.font = 'bold 10px "Courier New", monospace';
            ctx.textAlign = 'right';
            ctx.fillText(`${item.label}`, infoX - 50, iy);
            ctx.fillText(`${item.val}`, infoX, iy);
            iy += 16;
        });

        // ---- 位置/速度摘要 ----
        iy += 6;
        ctx.fillStyle = this.colors.textDim;
        ctx.font = '9px "Courier New", monospace';
        ctx.textAlign = 'right';
        ctx.fillText(`Pos  X:${p.x.toFixed(2)} Y:${p.y.toFixed(2)} Z:${p.z.toFixed(2)}`, infoX, iy); iy += 14;
        ctx.fillText(`Vel  X:${v.x.toFixed(2)} Y:${v.y.toFixed(2)} Z:${v.z.toFixed(2)}`, infoX, iy); iy += 14;
        ctx.fillText(`Trail:  ${this.trajectory.length} pts`, infoX, iy);

        ctx.restore();
    }

    // ==================== Canvas 初始化 ====================

    initCanvas(canvasId) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) return;
        this.ctx = this.canvas.getContext('2d');
        this.W = this.canvas.width;
        this.H = this.canvas.height;

        // 拖拽平移
        const onDown = (cx, cy) => {
            this.isDragging = true;
            this.lastMX = cx;
            this.lastMY = cy;
            this.canvas.style.cursor = 'grabbing';
        };
        const onMove = (cx, cy) => {
            if (!this.isDragging) return;
            const dx = (cx - this.lastMX) / this.zoom;
            const dy = (cy - this.lastMY) / this.zoom;
            this.panX -= dx;
            this.panY += dy;
            this.lastMX = cx;
            this.lastMY = cy;
            this.draw();
        };
        const onUp = () => {
            this.isDragging = false;
            this.canvas.style.cursor = 'crosshair';
        };

        this.canvas.addEventListener('mousedown', e => onDown(e.clientX, e.clientY));
        this.canvas.addEventListener('mousemove', e => onMove(e.clientX, e.clientY));
        this.canvas.addEventListener('mouseup', onUp);
        this.canvas.addEventListener('mouseleave', onUp);

        // 触摸
        this.canvas.addEventListener('touchstart', e => {
            if (e.touches.length === 1) onDown(e.touches[0].clientX, e.touches[0].clientY);
            if (e.touches.length === 2) {
                // 双指距离用于缩放
                this._pinchDist = Math.hypot(
                    e.touches[0].clientX - e.touches[1].clientX,
                    e.touches[0].clientY - e.touches[1].clientY
                );
            }
        }, { passive: true });
        this.canvas.addEventListener('touchmove', e => {
            if (e.touches.length === 1 && this.isDragging) {
                e.preventDefault();
                onMove(e.touches[0].clientX, e.touches[0].clientY);
            }
            if (e.touches.length === 2) {
                const d = Math.hypot(
                    e.touches[0].clientX - e.touches[1].clientX,
                    e.touches[0].clientY - e.touches[1].clientY
                );
                if (this._pinchDist) {
                    const scale = d / this._pinchDist;
                    this.zoom = Math.max(3, Math.min(300, this.zoom * scale));
                    this.draw();
                }
                this._pinchDist = d;
            }
        }, { passive: false });
        this.canvas.addEventListener('touchend', onUp);

        // 滚轮缩放
        this.canvas.addEventListener('wheel', e => {
            e.preventDefault();
            const factor = e.deltaY < 0 ? 1.12 : 0.89;
            this.zoom = Math.max(3, Math.min(300, this.zoom * factor));
            this.draw();
        });

        // 点击切换视图模式
        this.canvas.addEventListener('dblclick', e => {
            // 双击切换 all <-> top
            if (this.viewMode === 'all') {
                this.setView('top');
            } else {
                this.setView('all');
            }
        });

        this.canvas.style.cursor = 'crosshair';
    }

    // ==================== 视图切换 ====================

    setView(mode) {
        this.viewMode = mode;
        // 更新视图按钮状态
        const btns = document.querySelectorAll('.imu-view-btn');
        btns.forEach(btn => {
            btn.style.borderColor = btn.dataset.view === mode ? '#00ff88' : '#1a3a3a';
            btn.style.boxShadow = btn.dataset.view === mode ? '0 0 6px rgba(0,255,136,0.4)' : 'none';
            btn.style.color = btn.dataset.view === mode ? '#00ff88' : '#669988';
        });
        if (this.panelVisible) this.draw();
    }

    // ==================== 面板控制 ====================

    togglePanel(containerId) {
        const container = document.getElementById(containerId);
        if (!container) return;
        this.panelVisible = !this.panelVisible;
        container.style.display = this.panelVisible ? 'flex' : 'none';
        if (this.panelVisible) {
            this.initCanvas('imuTrajectoryCanvas');
            this.setView(this.viewMode);
            this._startLoop();
        } else {
            this._stopLoop();
        }
    }

    showPanel(containerId) {
        const container = document.getElementById(containerId);
        if (!container) return;
        this.panelVisible = true;
        container.style.display = 'flex';
        this.initCanvas('imuTrajectoryCanvas');
        this.setView(this.viewMode);
        this._startLoop();
    }

    hidePanel(containerId) {
        const container = document.getElementById(containerId);
        if (!container) return;
        this.panelVisible = false;
        container.style.display = 'none';
        this._stopLoop();
    }

    _startLoop() { this._stopLoop(); const l = () => { this.draw(); this.animationId = requestAnimationFrame(l); }; l(); }
    _stopLoop()  { if (this.animationId) { cancelAnimationFrame(this.animationId); this.animationId = null; } }
}

window.imuTrajectory = new IMUTrajectory();
