       // 添加额外的控制函数
       function forcePlayVideo() {
           console.log('Force playing video...');
           if (remoteVideo.srcObject) {
               const tracks = remoteVideo.srcObject.getVideoTracks();
               tracks.forEach(track => {
                   console.log('Before force - Track:', {
                       enabled: track.enabled,
                       muted: track.muted
                   });
                   track.enabled = true;
               });

               remoteVideo.muted = false;
               remoteVideo.play().catch(e => console.error('Force play failed:', e));
           }
       }

       function reloadVideoElement() {
           console.log('Reloading video element...');
           const parent = remoteVideo.parentNode;
           const newVideo = remoteVideo.cloneNode(true);
           newVideo.srcObject = remoteVideo.srcObject;
           parent.replaceChild(newVideo, remoteVideo);
           remoteVideo = newVideo;

           // 重新设置事件监听
           setupVideoEvents();
       }

       function setupVideoEvents() {
           remoteVideo.onloadeddata = () => console.log('Video data loaded');
           remoteVideo.oncanplay = () => console.log('Video can play');
           remoteVideo.onerror = (e) => console.error('Video error:', remoteVideo.error);
       }

       function playVideoWithRetry(retryCount = 0) {
           if (retryCount > 5) {
               console.error('Failed to play video after multiple attempts');
               showPlayButton();
               return;
           }

           // 确保视频元素就绪
           if (!remoteVideo.srcObject) {
               console.error('No video source available');
               return;
           }

           const videoTracks = remoteVideo.srcObject.getVideoTracks();
           if (videoTracks.length === 0) {
               console.error('No video tracks in stream');
               return;
           }

           console.log(`Play attempt ${retryCount + 1}, video readyState: ${remoteVideo.readyState}`);

           remoteVideo.play().then(() => {
               console.log('Video playback successful!');
               updateStatus('Video playing');

               // 检查视频是否真的在播放
               setTimeout(() => checkVideoPlayback(), 500);

           }).catch(err => {
               console.error(`Play attempt ${retryCount + 1} failed:`, err.name, err.message);

               if (err.name === 'NotAllowedError') {
                   updateStatus('Click anywhere to play video');
                   showPlayButton();
               } else if (err.name === 'NotSupportedError') {
                   updateStatus('Video format not supported');
                   console.error('Video format may not be supported by browser');
               } else {
                   // 其他错误，重试
                   setTimeout(() => playVideoWithRetry(retryCount + 1), 500);
               }
           });
       }

       function checkVideoPlayback() {
           if (remoteVideo.paused) {
               console.warn('Video is paused after successful play() call');
               updateStatus('Video paused unexpectedly');
           } else {
               console.log('Video is actively playing');
           }

           if (remoteVideo.videoWidth === 0 || remoteVideo.videoHeight === 0) {
               console.warn('Video dimensions are still 0x0 - possible decoding issue');

               // 检查轨道状态
               const tracks = remoteVideo.srcObject.getVideoTracks();
               tracks.forEach(track => {
                   console.log('Track debug:', {
                       id: track.id,
                       kind: track.kind,
                       enabled: track.enabled,
                       muted: track.muted,
                       readyState: track.readyState
                   });
               });
           } else {
               console.log(`Video dimensions: ${remoteVideo.videoWidth}x${remoteVideo.videoHeight}`);
               updateVideoInfo(remoteVideo.srcObject);
           }
       }

       function showPlayButton() {
           // 移除现有的播放按钮
           const existingButton = document.getElementById('manualPlayButton');
           if (existingButton) existingButton.remove();

           // 创建播放按钮
           const playButton = document.createElement('button');
           playButton.id = 'manualPlayButton';
           playButton.textContent = 'Click to Play Video';
           playButton.style.cssText = `
        position: fixed;
        top: 50%;
        left: 50%;
        transform: translate(-50%, -50%);
        padding: 15px 30px;
        font-size: 18px;
        background: #007bff;
        color: white;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        z-index: 1000;
    `;

           playButton.onclick = () => {
               remoteVideo.play().then(() => {
                   playButton.remove();
                   updateStatus('Video playing after manual start');
               }).catch(e => {
                   console.error('Manual play failed:', e);
                   updateStatus('Manual play failed: ' + e.message);
               });
           };

           document.body.appendChild(playButton);
       }