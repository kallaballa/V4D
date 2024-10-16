#!/bin/bash

title=$1
name=$2

cat << EOF
<!doctype html>
<html lang="en-us">
  <head>
    <title>${title}</title>
    <style>
      body {
        font-family: arial;
        margin: 0;
        padding: none;
      }

      .emscripten { padding-right: 0; margin-left: auto; margin-right: auto; display: block; }
      div.emscripten { text-align: center; }
      div.emscripten_border { border: 1px solid black; }
      /* the canvas *must not* have any border or padding, or mouse coords will be wrong */
      canvas.emscripten { border: 0px none; background-color: black; }

      #emscripten_logo {
        display: inline-block;
        margin: 0;
      }

      .spinner {
        height: 30px;
        width: 30px;
        margin: 0;
        margin-top: 20px;
        margin-left: 20px;
        display: inline-block;
        vertical-align: top;

        -webkit-animation: rotation .8s linear infinite;
        -moz-animation: rotation .8s linear infinite;
        -o-animation: rotation .8s linear infinite;
        animation: rotation 0.8s linear infinite;

        border-left: 5px solid rgb(235, 235, 235);
        border-right: 5px solid rgb(235, 235, 235);
        border-bottom: 5px solid rgb(235, 235, 235);
        border-top: 5px solid rgb(120, 120, 120);

        border-radius: 100%;
        background-color: rgb(189, 215, 46);
      }

      @-webkit-keyframes rotation {
        from {-webkit-transform: rotate(0deg);}
        to {-webkit-transform: rotate(360deg);}
      }
      @-moz-keyframes rotation {
        from {-moz-transform: rotate(0deg);}
        to {-moz-transform: rotate(360deg);}
      }
      @-o-keyframes rotation {
        from {-o-transform: rotate(0deg);}
        to {-o-transform: rotate(360deg);}
      }
      @keyframes rotation {
        from {transform: rotate(0deg);}
        to {transform: rotate(360deg);}
      }

      #status {
        display: inline-block;
        vertical-align: top;
        margin-top: 30px;
        margin-left: 20px;
        font-weight: bold;
        color: rgb(120, 120, 120);
      }

      #progress {
        height: 20px;
        width: 300px;
      }

     /* #controls {
        display: inline-block;
        float: right;
        vertical-align: top;
        margin-top: 30px;
        margin-right: 20px;
      }*/

      #output {
        width: 100%;
        height: 200px;
        margin: 0 auto;
        margin-top: 10px;
        border-left: 0px;
        border-right: 0px;
        padding-left: 0px;
        padding-right: 0px;
        display: block;
        color: white;
        font-family: 'Lucida Console', Monaco, monospace;
        outline: none;
      }
    </style>
  </head>
<body>
      <span id='controls'>
  <span><button id="captureBtn">Start Capture</button><button id="fullscreenBtn">Fullscreen</button>
  </span>
</span>
    <canvas id="v4dOffscreenCanvas" style="display:none;"></canvas>
    <video id="v4dVideoElement" autoplay style="display: none;"></video>

    <div class="emscripten" id="status">Downloading...</div>

    <div class="emscripten">
      <progress value="0" max="100" id="progress" hidden=1></progress>
    </div>


    <div class="emscripten_border">
      <canvas class="emscripten" id="canvas" oncontextmenu="event.preventDefault()" tabindex=-1></canvas>
    </div>
    <script type='text/javascript'>
      var statusElement = document.getElementById('status');
      var progressElement = document.getElementById('progress');
      var fsButton = document.querySelector("#fullscreenBtn");
      var captureBtn = document.querySelector("#captureBtn");
      var videoElement = document.querySelector("#v4dVideoElement");
      var offscreenCanvas = document.querySelector("#v4dOffscreenCanvas");
      var width = 960;
      var height = 960;
      function fixCanvasSize() {
          Module.canvas.style.width = (width / window.devicePixelRatio) + "px";
          Module.canvas.style.height = (height / window.devicePixelRatio) + "px";
          Module.canvas.width = width;
          Module.canvas.height = height;
          videoElement.style.width = (width / window.devicePixelRatio) + "px";
          videoElement.style.height = (height / window.devicePixelRatio) + "px";
          videoElement.width = width;
          videoElement.height = height;
          offscreenCanvas.style.width = (width / window.devicePixelRatio) + "px";
          offscreenCanvas.style.height = (height / window.devicePixelRatio) + "px";
          offscreenCanvas.width = width;
          offscreenCanvas.height = height;
      }

      var Module = {
        onRuntimeInitialized: function() {
          fixCanvasSize();
        },
        preRun: [],
        postRun: [],
        print: (function() {
          var element = document.getElementById('output');
          if (element) element.value = ''; // clear browser cache
          return function(text) {
            if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
            // These replacements are necessary if you render to raw HTML
            //text = text.replace(/&/g, "&amp;");
            //text = text.replace(/</g, "&lt;");
            //text = text.replace(/>/g, "&gt;");
            //text = text.replace('\n', '<br>', 'g');
            console.log(text);
            if (element) {
              element.value += text + "\n";
              element.scrollTop = element.scrollHeight; // focus on bottom
            }
          };
        })(),
        printErr: function(text) {
            if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
            console.error(text);
        },
        canvas: (function() {
          var canvas = document.getElementById('canvas');

          // As a default initial behavior, pop up an alert when webgl context is lost. To make your
          // application robust, you may want to override this behavior before shipping!
          // See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
          canvas.addEventListener("webglcontextlost", function(e) { alert('WebGL context lost. You will need to reload the page.'); e.preventDefault(); }, false);

          return canvas;
        })(),
        setStatus: function(text) {
          if (!Module.setStatus.last) Module.setStatus.last = { time: Date.now(), text: '' };
          if (text === Module.setStatus.last.text) return;
          var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
          var now = Date.now();
          if (m && now - Module.setStatus.last.time < 30) return; // if this is a progress update, skip it if too soon
          Module.setStatus.last.time = now;
          Module.setStatus.last.text = text;
          if (m) {
            text = m[1];
            progressElement.value = parseInt(m[2])*100;
            progressElement.max = parseInt(m[4])*100;
            progressElement.hidden = false;
          } else {
            progressElement.value = null;
            progressElement.max = null;
            progressElement.hidden = true;
          }
          statusElement.innerHTML = text;
        },
        totalDependencies: 0,
        monitorRunDependencies: function(left) {
          this.totalDependencies = Math.max(this.totalDependencies, left);
          Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies-left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
        }
      };
      Module.setStatus('Downloading...');

      var playing = false;
      var timeupdate = false;
      function checkReady() {
        if (playing && timeupdate) {
          globalThis.doCapture = true;
        }
      }

      captureBtn.addEventListener('click', async function() {
          let stream = await navigator.mediaDevices.getUserMedia({ video: { width: width, height: height } , audio: false });
          var settings = stream.getVideoTracks()[0].getSettings();
          var aspectRatio = settings.width / settings.height;
          Module._v4dInitCapture(width, width / aspectRatio);
          videoElement.addEventListener(
            "playing",
            () => {
              playing = true;
              checkReady();
            },
            true
          );

          videoElement.addEventListener(
            "timeupdate",
            () => {
              timeupdate = true;
              checkReady();
            },
            true
          );
          videoElement.srcObject = stream;
      });

      window.onerror = function(event) {
        // TODO: do not warn on ok events like simulating an infinite loop or exitStatus
        Module.setStatus('Exception thrown, see JavaScript console');
        //spinnerElement.style.display = 'none';
        Module.setStatus = function(text) {
          if (text) Module.printErr('[post-exception status] ' + text);
        };
      };

      fsButton.addEventListener('click', async function () {
        Module.requestFullscreen(false, false)
      });

       window.addEventListener('fullscreenchange', function (event) {
          if (document.fullscreenElement) {
              console.log("Element: " + document.fullscreenElement.id + " entered fullscreen mode.");
          } else {
              console.log('Leaving fullscreen mode.');
          }
      });
    </script>
    <script async type="text/javascript" src="example_v4d_${name}.js"></script>
</body>
</html>
EOF
