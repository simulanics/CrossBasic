' opengl_torus.xs
' Demo: Render a rotating torus with XOpenGLSurface


Const GL_COLOR_BUFFER_BIT       = 16384     ' 0x00004000
Const GL_DEPTH_BUFFER_BIT       = 256       ' 0x00000100
Const GL_TRIANGLES              = 4         ' 0x0004
Const GL_STATIC_DRAW            = 35044     ' 0x88E4
Const GL_ARRAY_BUFFER           = 34962     ' 0x8892
Const GL_ELEMENT_ARRAY_BUFFER   = 34963     ' 0x8893
Const GL_FLOAT                  = 5126      ' 0x1406
Const GL_UNSIGNED_INT           = 5125      ' 0x1405
Const GL_VERTEX_SHADER   = 35633   ' 0x8B31
Const GL_FRAGMENT_SHADER = 35632   ' 0x8B30


' 1. Create host window
' Var win As New XWindow
' win.Title              = "CrossBasic OpenGL Torus Demo"
' win.Width              = 800
' win.Height             = 600
' win.HasCloseButton     = True
' win.Resizable          = True

' win.show()

' 2. Load our plugin
Var gl As New XOpenGLSurface
'gl.parent = win.handle

If gl.Init(1024, 768, "OpenGL") = False Then
  MessageBox("Failed to initialize OpenGL")
  Quit()
End If

' 3. Define shaders
Dim fragmentSrc As String = _
"#version 330 core" + EndOfLine + _
"in vec3 FragPos;" + EndOfLine + _
"in vec3 Normal;" + EndOfLine + _
"" + EndOfLine + _
"uniform vec3 lightPos;" + EndOfLine + _
"uniform vec3 viewPos;" + EndOfLine + _
"" + EndOfLine + _
"out vec4 FragColor;" + EndOfLine + _
"" + EndOfLine + _
"void main() {" + EndOfLine + _
"  vec3 norm = normalize(Normal);" + EndOfLine + _
"  vec3 lightDir = normalize(lightPos - FragPos);" + EndOfLine + _
"  float diff = max(dot(norm, lightDir), 0.0);" + EndOfLine + _
"  vec3 diffuse = diff * vec3(1.0);" + EndOfLine + _
"  vec3 ambient = 0.1 * vec3(1.0);" + EndOfLine + _
"  FragColor = vec4(ambient + diffuse, 1.0);" + EndOfLine + _
"}"

// 3. Define shaders
Dim vertexSrc As String = _
"#version 330 core" + EndOfLine + _
"layout(location = 0) in vec3 aPos;" + EndOfLine + _
"layout(location = 1) in vec3 aNormal;" + EndOfLine + _
"" + EndOfLine + _
"uniform mat4 model;" + EndOfLine + _
"uniform mat4 view;" + EndOfLine + _
"uniform mat4 projection;" + EndOfLine + _
"" + EndOfLine + _
"out vec3 FragPos;" + EndOfLine + _
"out vec3 Normal;" + EndOfLine + _
"" + EndOfLine + _
"void main() {" + EndOfLine + _
"  FragPos = vec3(model * vec4(aPos, 1.0));" + EndOfLine + _
"  Normal  = mat3(transpose(inverse(model))) * aNormal;" + EndOfLine + _
"  gl_Position = projection * view * vec4(FragPos, 1.0);" + EndOfLine + _
"}"


' 4. Compile & link program
Var vs As Integer = gl.CompileShader(GL_VERTEX_SHADER, vertexSrc)
Var fs As Integer = gl.CompileShader(GL_FRAGMENT_SHADER, fragmentSrc)
Var prog As Integer = gl.LinkProgram(vs, fs)
gl.UseProgram(prog)

' 5. Generate torus geometry in-script
Const numMajor = 32
Const numMinor = 32
Const majorR = 1.0
Const minorR = 0.4

Dim verts() As Double
Dim norms() As Double
Dim idxs() As Integer

For i As Integer = 0 To numMajor
  Dim a0 as Double = i * 2 * PI / numMajor
  Dim x0 as Double = Cos(a0)
  Dim y0 as Double = Sin(a0)

  For j As Integer = 0 To numMinor
    Dim a1 as Double = j * 2 * PI / numMinor
    Dim x1 as Double = Cos(a1)
    Dim y1 as Double= Sin(a1)
    verts.add((majorR + minorR * x1) * x0)
    verts.add((majorR + minorR * x1) * y0)
    verts.add(minorR * y1)
    norms.add(x1 * x0)
    norms.add(x1 * y0)
    norms.add(y1)
  Next j
Next i

For i As Integer = 0 To numMajor - 1
  For j As Integer = 0 To numMinor - 1
    Dim a as Integer = i * (numMinor + 1) + j
    Dim b as Integer = ((i + 1) Mod numMajor) * (numMinor + 1) + j
    Dim c as Integer = b + 1
    Dim d as Integer = a + 1
    idxs.add(a)
    idxs.add(b)
    idxs.add(d)
    idxs.add(b)
    idxs.add(c)
    idxs.add(d)
  Next j
Next i

'6. Upload to GPU
Dim vao As Integer = gl.CreateVertexArray()
gl.BindVertexArray(vao)

Dim vboPos As Integer = gl.CreateBuffer()
gl.BindBuffer(GL_ARRAY_BUFFER, vboPos)
gl.BufferData(GL_ARRAY_BUFFER, verts, verts.LastIndex() * 8, GL_STATIC_DRAW)
gl.VertexAttribPointer(0, 3, GL_FLOAT, False, 0, 0)
gl.EnableVertexAttrib(0)

Dim vboNorm As Integer = gl.CreateBuffer()
gl.BindBuffer(GL_ARRAY_BUFFER, vboNorm)
gl.BufferData(GL_ARRAY_BUFFER, norms, norms.LastIndex() * 8, GL_STATIC_DRAW)
gl.VertexAttribPointer(1, 3, GL_FLOAT, False, 0, 0)
gl.EnableVertexAttrib(1)

Dim ibo As Integer = gl.CreateBuffer()
gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo)
gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, idxs, idxs.LastIndex() * 4, GL_STATIC_DRAW)

// 7. Main loop
While gl.ShouldClose() = False
  gl.Clear(GL_COLOR_BUFFER_BIT Or GL_DEPTH_BUFFER_BIT)
  gl.UseProgram(prog)

  // Update transforms
  Dim t = gl.GetTime()
  Dim model() As Double '' model(16)

  model = gl.IdentityMatrix4()
  model = gl.RotateMatrix4(model, t, 0.5, 1.0, 0.0)
  gl.UniformMatrix4fv(gl.GetUniformLocation(prog, "model"), 1, False, model)
  
  Dim view() As Double ' view(16)
  
  view = gl.LookAtMatrix4(0,0,5, 0,0,0, 0,1,0) 
  gl.UniformMatrix4fv(gl.GetUniformLocation(prog, "view"), 1, False, view)

  Dim proj() as Double ' proj(16)


 proj = gl.PerspectiveMatrix4(45, 1024 / 768, 0.1, 100) 

  gl.UniformMatrix4fv(gl.GetUniformLocation(prog, "projection"), 1, False, proj)

  gl.Uniform3f(gl.GetUniformLocation(prog, "lightPos"), 3.0, 3.0, 3.0)
  gl.Uniform3f(gl.GetUniformLocation(prog, "viewPos"), 0.0, 0.0, 5.0)

  gl.DrawElements(GL_TRIANGLES, idxs.LastIndex() + 1, GL_UNSIGNED_INT, 0)
  gl.SwapGLBuffers()   // pass window handle if plugin supports embed
  gl.PollEvents()
Wend

' while True
'  Doevents(1)
' wend

'Quit()
