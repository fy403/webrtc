// Curve Editor Controller
(function () {
  // State
  let currentCurveId = null;
  let editingPoints = [];
  let isDragging = false;
  let dragPointIndex = -1;
  let canvas, ctx;
  
  // Constants
  const BASE_MAX_TIME = 2000; // Base maximum time in ms
  const PADDING = 50;
  
  // Dynamic max time based on curve points
  function getMaxTime() {
    if (editingPoints.length === 0) return BASE_MAX_TIME;
    const maxPointTime = Math.max(...editingPoints.map(p => p.t));
    // Round up to nearest 500ms
    return Math.max(BASE_MAX_TIME, Math.ceil(maxPointTime / 500) * 500);
  }
  
  // Initialize
  function init() {
    canvas = document.getElementById('curveCanvas');
    ctx = canvas.getContext('2d');
    
    // Set canvas size
    resizeCanvas();
    window.addEventListener('resize', resizeCanvas);
    
    // Canvas interaction
    canvas.addEventListener('mousedown', handleMouseDown);
    canvas.addEventListener('mousemove', handleMouseMove);
    canvas.addEventListener('mouseup', handleMouseUp);
    canvas.addEventListener('mouseleave', handleMouseUp);
    
    // Touch support
    canvas.addEventListener('touchstart', handleTouchStart, { passive: false });
    canvas.addEventListener('touchmove', handleTouchMove, { passive: false });
    canvas.addEventListener('touchend', handleMouseUp);
    
    // Load initial curve
    const savedCurveId = localStorage.getItem('speedCurveId') || 'linear';
    loadCurve(savedCurveId);
    
    // Render curve list
    renderCurveList();
  }
  
  function resizeCanvas() {
    const container = canvas.parentElement;
    canvas.width = container.clientWidth - 40;
    canvas.height = 400;
    render();
  }
  
  // Curve Management
  function loadCurve(id) {
    currentCurveId = id;
    const curve = speedCurveManager.getCurve(id);
    
    if (!curve) {
      console.error('Curve not found:', id);
      return;
    }
    
    // Update UI
    const nameInput = document.getElementById('curveName');
    const descInput = document.getElementById('curveDesc');
    
    nameInput.value = curve.name || '';
    descInput.value = curve.description || '';
    
    // Enable/disable inputs based on whether it's a preset or custom
    const isCustom = curve.isCustom;
    nameInput.disabled = !isCustom;
    descInput.disabled = !isCustom;
    
    // Clone points for editing
    editingPoints = JSON.parse(JSON.stringify(curve.points));
    
    renderCurveList();
    render();
    renderPointList();
  }
  
  function saveCurve() {
    if (!currentCurveId) return;

    const curve = speedCurveManager.getCurve(currentCurveId);
    const isCustom = curve?.isCustom;

    if (isCustom) {
      // Update custom curve
      speedCurveManager.updateCustomCurve(currentCurveId, {
        name: document.getElementById('curveName').value,
        description: document.getElementById('curveDesc').value,
        points: editingPoints
      });
      // Apply the updated curve
      speedCurveManager.setCurve(currentCurveId);
      showToast('曲线已保存并应用');
    } else {
      // Apply preset to current
      speedCurveManager.setCurve(currentCurveId);
      showToast(`已应用曲线: ${curve.name}`);
    }

    renderCurveList();

    // Trigger a storage event manually to notify other pages (in case the browser doesn't fire it automatically)
    // Use setTimeout to ensure the localStorage write is complete
    setTimeout(() => {
      window.dispatchEvent(new StorageEvent('storage', {
        key: 'speedCurveId',
        newValue: speedCurveManager.currentCurveId,
        url: window.location.href
      }));
      // Also trigger for custom curves if applicable
      if (isCustom) {
        window.dispatchEvent(new StorageEvent('storage', {
          key: 'customSpeedCurves',
          url: window.location.href
        }));
      }
    }, 100);
  }
  
  function resetCurve() {
    if (!currentCurveId) return;
    
    const curve = speedCurveManager.getCurve(currentCurveId);
    if (curve && !curve.isCustom) {
      // Can't reset preset
      showToast('预设曲线无法重置');
      return;
    }
    
    // Reset to linear curve
    editingPoints = JSON.parse(JSON.stringify(speedCurveManager.presets.linear.points));
    render();
    renderPointList();
    showToast('曲线已重置');
  }
  
  function createNewCurve() {
    const nameInput = document.getElementById('newCurveName');
    const name = nameInput.value.trim();
    
    if (!name) {
      showToast('请输入曲线名称');
      return;
    }
    
    // Create with default linear points
    const id = speedCurveManager.createCustomCurve(
      name,
      JSON.parse(JSON.stringify(speedCurveManager.presets.linear.points)),
      '自定义加速曲线'
    );
    
    nameInput.value = '';
    loadCurve(id);
    showToast('新曲线已创建');
  }
  
  function deleteCurve(id, event) {
    event.stopPropagation();
    
    const curve = speedCurveManager.getCurve(id);
    if (!curve?.isCustom) {
      showToast('无法删除预设曲线');
      return;
    }
    
    if (confirm(`确定要删除 "${curve.name}" 吗?`)) {
      speedCurveManager.deleteCustomCurve(id);
      if (currentCurveId === id) {
        loadCurve('linear');
      }
      renderCurveList();
      showToast('曲线已删除');
    }
  }

  function exportCurve() {
    if (!currentCurveId) {
      showToast('没有选中的曲线');
      return;
    }

    const curve = speedCurveManager.getCurve(currentCurveId);
    if (!curve) {
      showToast('曲线数据无效');
      return;
    }

    // Create export data
    const exportData = {
      version: '1.0',
      name: curve.name,
      description: curve.description || '',
      points: curve.points,
      exportedAt: new Date().toISOString()
    };

    // Convert to JSON string
    const jsonString = JSON.stringify(exportData, null, 2);

    // Copy to clipboard
    navigator.clipboard.writeText(jsonString).then(() => {
      showToast(`曲线 "${curve.name}" 已复制到剪贴板`);
      console.log('[Export] Curve exported successfully:', exportData.name);
    }).catch(err => {
      console.error('[Export] Failed to copy to clipboard:', err);
      showToast('复制失败，请手动复制');
    });
  }

  function showImportDialog() {
    const importText = prompt('请粘贴曲线数据 (JSON格式):');
    if (!importText || importText.trim() === '') {
      return;
    }

    try {
      // Parse JSON
      const importData = JSON.parse(importText);

      // Validate format
      if (!importData.name || !importData.points || !Array.isArray(importData.points)) {
        throw new Error('无效的曲线数据格式');
      }

      // Validate points
      SpeedCurve.validatePoints(importData.points);

      // Ask for curve name
      const name = prompt('请输入新曲线的名称:', importData.name);
      if (!name || name.trim() === '') {
        showToast('导入已取消');
        return;
      }

      // Create new custom curve
      const id = speedCurveManager.createCustomCurve(
        name,
        importData.points,
        importData.description || ''
      );

      loadCurve(id);
      showToast(`曲线 "${name}" 导入成功`);
      console.log('[Import] Curve imported successfully:', name);

    } catch (err) {
      console.error('[Import] Failed to import curve:', err);
      showToast('导入失败: ' + err.message);
    }
  }
  
  // Point Management
  function addPoint() {
    // Add point at the end with interpolated value
    const lastPoint = editingPoints[editingPoints.length - 1];
    const newPoint = {
      t: lastPoint ? lastPoint.t + 200 : 200,
      v: 1.0
    };
    editingPoints.push(newPoint);
    
    // Sort by time
    editingPoints.sort((a, b) => a.t - b.t);
    
    render();
    renderPointList();
  }
  
  function removePoint(index) {
    if (editingPoints.length <= 2) {
      showToast('至少需要2个控制点');
      return;
    }
    
    editingPoints.splice(index, 1);
    render();
    renderPointList();
  }
  
  function updatePoint(index, field, value) {
    const numValue = parseFloat(value);
    if (isNaN(numValue)) return;
    
    if (field === 't') {
      editingPoints[index].t = Math.max(0, numValue);
    } else if (field === 'v') {
      // Allow up to 2 decimal places for precision (no grid snapping)
      editingPoints[index].v = Math.round(Math.max(0, Math.min(1, numValue)) * 100) / 100;
    }
    
    // Sort by time after update
    editingPoints.sort((a, b) => a.t - b.t);
    
    render();
    renderPointList();
  }
  
  // Canvas Rendering
  function render() {
    if (!ctx) return;
    
    const width = canvas.width;
    const height = canvas.height;
    const graphWidth = width - PADDING * 2;
    const graphHeight = height - PADDING * 2;
    const maxTime = getMaxTime();
    
    // Clear canvas completely
    ctx.clearRect(0, 0, width, height);
    
    // Fill background
    ctx.fillStyle = 'rgba(0, 0, 0, 0.3)';
    ctx.fillRect(0, 0, width, height);
    
    // Draw grid
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
    ctx.lineWidth = 1;
    
    // Calculate time step based on max time
    const timeStep = calculateTimeStep(maxTime);
    const timeGridCount = Math.ceil(maxTime / timeStep);
    
    // Vertical grid lines (time)
    for (let i = 0; i <= timeGridCount; i++) {
      const timeValue = i * timeStep;
      const x = PADDING + (timeValue / maxTime) * graphWidth;
      ctx.beginPath();
      ctx.moveTo(x, PADDING);
      ctx.lineTo(x, height - PADDING);
      ctx.stroke();
      
      // Time labels
      ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
      ctx.font = '12px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(`${timeValue}ms`, x, height - PADDING + 20);
    }
    
    // Horizontal grid lines (speed) - 0.2 steps (from bottom: 0 to top: 1.0)
    for (let i = 0; i <= 5; i++) {
      const speedValue = i * 0.2;
      const y = (height - PADDING) - (speedValue) * graphHeight;
      ctx.beginPath();
      ctx.moveTo(PADDING, y);
      ctx.lineTo(width - PADDING, y);
      ctx.stroke();
      
      // Speed labels - 0 at bottom, 1.0 at top
      ctx.fillStyle = 'rgba(255, 255, 255, 0.5)';
      ctx.font = '12px Arial';
      ctx.textAlign = 'right';
      const displayValue = speedValue === 0 ? '0' : (speedValue === 1 ? '1.0' : speedValue.toFixed(1));
      ctx.fillText(displayValue, PADDING - 10, y + 4);
    }
    
    // Draw axis labels
    ctx.fillStyle = '#00d4ff';
    ctx.font = 'bold 14px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('时间 (ms)', width / 2, height - 5);
    
    ctx.save();
    ctx.translate(15, height / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText('速度比例', 0, 0);
    ctx.restore();
    
    // Draw curve
    if (editingPoints.length > 0) {
      drawCurve(editingPoints, '#00d4ff', 3);
    }
    
    // Draw control points
    editingPoints.forEach((point, index) => {
      const x = PADDING + (point.t / maxTime) * graphWidth;
      const y = (height - PADDING) - (point.v) * graphHeight;
      
      // Point circle
      ctx.beginPath();
      ctx.arc(x, y, 8, 0, Math.PI * 2);
      ctx.fillStyle = index === 0 || index === editingPoints.length - 1 ? '#ff6b6b' : '#00d4ff';
      ctx.fill();
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 2;
      ctx.stroke();
      
      // Point label
      ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
      ctx.font = '11px Arial';
      ctx.textAlign = 'center';
      ctx.fillText(`P${index}`, x, y - 15);
    });
  }
  
  // Calculate optimal time step based on max time
  function calculateTimeStep(maxTime) {
    if (maxTime <= 500) return 50;
    if (maxTime <= 1000) return 100;
    if (maxTime <= 2000) return 200;
    if (maxTime <= 5000) return 500;
    if (maxTime <= 10000) return 1000;
    return 2000;
  }
  
  function drawCurve(points, color, lineWidth) {
    if (points.length < 2) return;
    
    const width = canvas.width;
    const height = canvas.height;
    const graphWidth = width - PADDING * 2;
    const graphHeight = height - PADDING * 2;
    const maxTime = getMaxTime();
    
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = lineWidth;
    
    // Move to first point
    const firstX = PADDING + (points[0].t / maxTime) * graphWidth;
    const firstY = (height - PADDING) - (points[0].v) * graphHeight;
    ctx.moveTo(firstX, firstY);
    
    // Draw lines between points
    for (let i = 1; i < points.length; i++) {
      const x = PADDING + (points[i].t / maxTime) * graphWidth;
      const y = (height - PADDING) - (points[i].v) * graphHeight;
      ctx.lineTo(x, y);
    }
    
    ctx.stroke();
  }
  
  // Mouse/Touch Interaction
  function getCanvasPosition(e) {
    const rect = canvas.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    return {
      x: clientX - rect.left,
      y: clientY - rect.top
    };
  }
  
  function findPointAtPosition(pos) {
    const width = canvas.width;
    const height = canvas.height;
    const graphWidth = width - PADDING * 2;
    const graphHeight = height - PADDING * 2;
    const maxTime = getMaxTime();
    
    for (let i = 0; i < editingPoints.length; i++) {
      const point = editingPoints[i];
      const x = PADDING + (point.t / maxTime) * graphWidth;
      const y = (height - PADDING) - (point.v) * graphHeight;
      
      const dist = Math.sqrt(Math.pow(pos.x - x, 2) + Math.pow(pos.y - y, 2));
      if (dist < 15) {
        return i;
      }
    }
    return -1;
  }
  
  function handleMouseDown(e) {
    const pos = getCanvasPosition(e);
    dragPointIndex = findPointAtPosition(pos);
    
    if (dragPointIndex >= 0) {
      isDragging = true;
      canvas.style.cursor = 'grabbing';
    }
  }
  
  function handleMouseMove(e) {
    const pos = getCanvasPosition(e);
    const maxTime = getMaxTime();
    
    if (isDragging && dragPointIndex >= 0) {
      // Don't allow dragging first or last point (must stay at v=0 and v=1)
      if (dragPointIndex === 0) {
        editingPoints[0].t = 0;
        editingPoints[0].v = 0;
        return;
      }
      if (dragPointIndex === editingPoints.length - 1) {
        editingPoints[dragPointIndex].v = 1;
        // Allow adjusting time of last point
        const width = canvas.width;
        const graphWidth = width - PADDING * 2;
        const t = ((pos.x - PADDING) / graphWidth) * maxTime;
        editingPoints[dragPointIndex].t = Math.max(editingPoints[dragPointIndex - 1]?.t + 50 || 0, t);
        render();
        renderPointList();
        return;
      }
      
      const width = canvas.width;
      const height = canvas.height;
      const graphWidth = width - PADDING * 2;
      const graphHeight = height - PADDING * 2;
      
      let t = ((pos.x - PADDING) / graphWidth) * maxTime;
      let v = ((height - PADDING) - pos.y) / graphHeight;
      
      // Clamp values
      t = Math.max(0, t);
      v = Math.max(0, Math.min(1, v));
      
      // No grid snapping for custom points - allow free positioning
      // Round v to 2 decimal places for precision
      v = Math.round(v * 100) / 100;
      
      // Ensure time is between neighbors
      if (dragPointIndex > 0) {
        t = Math.max(editingPoints[dragPointIndex - 1].t + 10, t);
      }
      if (dragPointIndex < editingPoints.length - 1) {
        t = Math.min(editingPoints[dragPointIndex + 1].t - 10, t);
      }
      
      editingPoints[dragPointIndex] = { t, v };
      
      render();
      renderPointList();
    } else {
      // Update cursor
      const pointIndex = findPointAtPosition(pos);
      canvas.style.cursor = pointIndex >= 0 ? 'grab' : 'crosshair';
    }
  }
  
  function handleMouseUp() {
    isDragging = false;
    dragPointIndex = -1;
    canvas.style.cursor = 'crosshair';
  }
  
  function handleTouchStart(e) {
    e.preventDefault();
    handleMouseDown(e);
  }
  
  function handleTouchMove(e) {
    e.preventDefault();
    handleMouseMove(e);
  }
  
  // UI Rendering
  function renderCurveList() {
    const container = document.getElementById('curveList');
    const curves = speedCurveManager.getAllCurves();
    
    container.innerHTML = '';
    
    Object.entries(curves).forEach(([id, curve]) => {
      const item = document.createElement('div');
      item.className = `curve-item ${id === currentCurveId ? 'active' : ''}`;
      item.onclick = () => loadCurve(id);
      
      let actions = '';
      if (curve.isCustom) {
        actions = `
          <div class="curve-item-actions">
            <button class="btn-small btn-delete" onclick="deleteCurve('${id}', event)">删除</button>
          </div>
        `;
      }
      
      item.innerHTML = `
        <div class="curve-item-name">${curve.name}</div>
        <div class="curve-item-desc">${curve.description || '无描述'}</div>
        ${curve.game ? `<div class="curve-item-game">🎮 ${curve.game}</div>` : ''}
        ${actions}
      `;
      
      container.appendChild(item);
    });
  }
  
  function renderPointList() {
    const container = document.getElementById('pointList');
    
    container.innerHTML = editingPoints.map((point, index) => {
      const isFirst = index === 0;
      const isLast = index === editingPoints.length - 1;
      
      // Display v with up to 2 decimal places
      const vDisplay = point.v.toFixed(2);
      
      return `
        <div class="point-row">
          <div class="point-label">P${index}</div>
          <input type="number" 
                 class="point-input" 
                 value="${Math.round(point.t)}" 
                 onchange="updatePoint(${index}, 't', this.value)"
                 ${isFirst ? 'disabled' : ''}>
          <input type="number" 
                 class="point-input" 
                 value="${vDisplay}" 
                 step="0.01"
                 onchange="updatePoint(${index}, 'v', this.value)"
                 ${isFirst || isLast ? 'disabled' : ''}>
          <button class="point-remove" 
                  onclick="removePoint(${index})"
                  ${isFirst || isLast ? 'disabled' : ''}>×</button>
        </div>
      `;
    }).join('');
  }
  
  function showToast(message) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('show');
    
    setTimeout(() => {
      toast.classList.remove('show');
    }, 3000);
  }
  
  // Initialize on load
  window.addEventListener('load', init);
  
  // Expose functions
  window.loadCurve = loadCurve;
  window.saveCurve = saveCurve;
  window.resetCurve = resetCurve;
  window.createNewCurve = createNewCurve;
  window.deleteCurve = deleteCurve;
  window.addPoint = addPoint;
  window.removePoint = removePoint;
  window.updatePoint = updatePoint;
  window.exportCurve = exportCurve;
  window.showImportDialog = showImportDialog;
})();
