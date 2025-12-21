Module Foo
  Function Bar() As Integer
    Return 1
  End Function        // comment
End Module            // <- comment this out to see the same crash
print("hi")
print(str(Bar()))
