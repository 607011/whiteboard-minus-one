// Copyright (c) 2012-2015 Oliver Lau <ola@ct.de>
// All rights reserved.

#ifdef GL_ES
precision mediump int;
precision mediump float;
#endif

varying vec4 vTexCoord;
uniform sampler2D uVideoTexture;
uniform sampler2D uDepthTexture;
// uniform sampler2D uImageTexture;
uniform float uGamma;
uniform float uContrast;
uniform float uSaturation;

void main(void)
{
  vec3 color;
  float depth = texture2D(uDepthTexture, vTexCoord.st).r;
  if (depth == 0.0) {
    color = texture2D(uVideoTexture, vTexCoord.st).rgb;
    color = pow(color, vec3(1.0 / uGamma));
    color = (color - 0.5) * uContrast + 0.5;
    float luminance = dot(color, vec3(0.2125, 0.7154, 0.0721));
    vec3 gray = vec3(luminance);
    color = mix(gray, color, uSaturation);
  }
  else {
    color = vec3(0.2, 1.0, 0.5);
  }
  gl_FragColor = vec4(color, 1.0);
}
