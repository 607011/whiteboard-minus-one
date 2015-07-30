// Copyright (c) 2015 Oliver Lau <ola@ct.de>
// All rights reserved.

#version 130
#extension GL_EXT_gpu_shader4 : enable

smooth in vec2 vTexCoord;
uniform usampler2D uDepthTexture;
uniform sampler2D uVideoTexture;
uniform sampler2D uMapTexture;
uniform float uGamma;
uniform float uContrast;
uniform float uSaturation;
uniform float uFarThreshold;
uniform float uNearThreshold;

void main(void)
{
  vec3 color;
  float depth = float(texture2D(uDepthTexture, vTexCoord).x);
  if (depth == 0.0) {
    color = vec3(0.345, 0.980, 0.173); // green
  }
  else if (depth > uFarThreshold) {
    color = vec3(0.345, 0.173, 0.980); // blue
  }
  else if (depth < uNearThreshold) {
    color = vec3(0.980, 0.173, 0.345); // red
  }
  else {
    color = texture2D(uVideoTexture, vTexCoord).rgb;
    color = pow(color, vec3(1.0 / uGamma));
    color = (color - 0.5) * uContrast + 0.5;
    float luminance = dot(color, vec3(0.2125, 0.7154, 0.0721));
    vec3 gray = vec3(luminance);
    color = mix(gray, color, uSaturation);
  }
  gl_FragColor = vec4(color, 1.0);
}
