// Copyright (c) 2012-2015 Oliver Lau <ola@ct.de>
// All rights reserved.

#ifdef GL_ES
precision mediump int;
precision mediump float;
#endif

attribute vec4 aVertex;
attribute vec4 aTexCoord;
varying vec4 vTexCoord;
uniform mat4 uMatrix;

void main(void)
{
  vTexCoord = aTexCoord;
  gl_Position = uMatrix * aVertex;
}
