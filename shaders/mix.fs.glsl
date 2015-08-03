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
uniform vec2 uHalo[1024];
uniform int uHaloSize;
uniform float uFarThreshold;
uniform float uNearThreshold;
uniform bool uRenderForFBO;

const ivec2 iDepthSize = ivec2(512, 424);
const vec2 fDepthSize = vec2(iDepthSize);

const float MaxDepth = 6500.0;

bool allDepthsValidWithinHalo(vec2 coord) {
  for (int i = 0; i < uHaloSize; ++i) {
    float depth = float(texture2D(uDepthTexture, coord + uHalo[i]).r);
    if (depth < uNearThreshold || depth > uFarThreshold)
      return false;
  }
  return true;
}


void main(void)
{
  vec3 color = vec3(0.0);
  ivec2 dsp = texture2D(uMapTexture, vTexCoord).xy;
  vec2 coord = vec2(dsp) / fDepthSize;
  if (dsp.x >= 0 && dsp.y >= 0 && dsp.x < iDepthSize.x && dsp.y < iDepthSize.y && allDepthsValidWithinHalo(coord)) {
    color = texture2D(uVideoTexture, vTexCoord).rgb;
    // gamma correction
    color = pow(color, vec3(1.0 / uGamma));
    // saturation
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 gray = vec3(luminance);
    color = mix(gray, color, uSaturation);
    // contrast
    color = (color - 0.5) * uContrast + 0.5;
  }
  else {
    color = texture2D(uImageTexture, vTexCoord).rgb;
  }
  gl_FragColor = vec4(color, 1.0);
}
