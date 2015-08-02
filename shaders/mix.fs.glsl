// Copyright (c) 2015 Oliver Lau <ola@ct.de>
// All rights reserved.

#version 130
#extension GL_EXT_gpu_shader4 : enable

smooth in vec2 vTexCoord;
uniform usampler2D uDepthTexture;
uniform sampler2D uVideoTexture;
uniform isampler2D uMapTexture;
uniform sampler2D uImageTexture;
uniform float uGamma;
uniform float uContrast;
uniform float uSaturation;
uniform float uSharpen[9];
uniform vec2 uOffset[9];
uniform float uFarThreshold;
uniform float uNearThreshold;
uniform bool uRenderForFBO;

const ivec2 iDepthSize = ivec2(512, 424);
const vec2 fDepthSize = vec2(iDepthSize);
//const vec2 fColorSize = vec2(1920.0, 1080.0);

void main(void)
{
  vec3 color = texture2D(uVideoTexture, vTexCoord).rgb;
  ivec2 dsp = texture2D(uMapTexture, vTexCoord).xy;
  if (dsp.x < 0 || dsp.y < 0 || dsp.x > iDepthSize.x || dsp.y > iDepthSize.y)
    discard;
  vec2 coord = vec2(dsp) / fDepthSize;
  float depth = float(texture2D(uDepthTexture, coord).r);
  if (depth > uNearThreshold && depth < uFarThreshold) {
    color = texture2D(uVideoTexture, vTexCoord).rgb;
    if (!uRenderForFBO) {
      // for (int i = 0; i < 9; ++i) {
      //   vec3 c = texture2D(uVideoTexture, vTexCoord + uOffset[i] / fColorSize).rgb;
      //   color += c * uSharpen[i];
      // }
      color = pow(color, vec3(1.0 / uGamma));
      float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
      vec3 gray = vec3(luminance);
      color = mix(gray, color, uSaturation);
      color = (color - 0.5) * uContrast + 0.5;
    }
  }
  else if (depth == 0.0) {
    color = vec3(0.7, 1.0, 0.3);
  }
  else {
    color = texture2D(uImageTexture, vTexCoord).rgb;
  }
  gl_FragColor = vec4(color, 1.0);
}
