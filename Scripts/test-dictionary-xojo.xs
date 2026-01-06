// -----------------------------------------------------------------------------
// Demo: Custom Dictionary Class in CrossBasic
// This example defines a simple `Dictionary` class using parallel arrays to store
// string keys and variant values. It supports adding, updating, retrieving, and
// enumerating key-value pairs, emulating a basic associative array behavior.
// -----------------------------------------------------------------------------

//----------------------------------------------------------------
// Dictionary class – stores key-value pairs using dynamic arrays.
//----------------------------------------------------------------
Class Dictionary

  // Properties: parallel arrays for keys and values
  Dim data() As String    // Array of keys
  Dim vals() As Variant   // Array of corresponding values

  // Constructor: initializes empty arrays
  Sub Constructor()
    data = Array()        // Initialize key array
    vals = Array()        // Initialize value array
  End Sub

  // Count method: returns number of key-value pairs
  Function Count() As Integer
    Return data.Count()
  End Function

  // SetItem method: updates value if key exists, or adds a new pair
  Sub Value(key As String, Assigns value As Variant)
    Dim found As Boolean = False
    For i As Integer = 0 To self.Count() - 1
      If data(i) = key Then
        vals(i) = value   // Update existing key's value
        found = True
      End If
    Next

    If found = False Then
      data.Add(key)       // Add new key
      vals.Add(value)     // Add new value
    End If
  End Sub

  // GetItem method: returns value for a key, or "nil" if key not found
  Function Value(key As String) As Variant
    For i As Integer = 0 To data.Count() - 1
      If data(i) = key Then
        Return vals(i)
      End If
    Next
    Return "nil"
  End Function

  // Keys method: returns array of all keys
  Function Keys() As String
    Return data
  End Function

End Class

//----------------------------------------------------------------
// Test code for Dictionary
//----------------------------------------------------------------

// Create a new Dictionary instance which calls it's Constructor() from Dictionary() - no parameters passed to dictionary
Dim dt As New Dictionary

// Add several key–value pairs to the dictionary
dt.Value("name") = "Alice"
dt.Value("age") = 30
dt.Value("city") = "Wonderland"

// Retrieve and display specific values
Print("Name: " + dt.Value("name"))
Print("Age: " + Str(dt.Value("age")))
Print("City: " + dt.Value("city"))

// Print total number of entries
Print("Total items: " + Str(dt.Count()))

// Enumerate all keys and print their associated values
Dim keys() As String = dt.Keys()
For i As Integer = 0 To keys.Count() - 1
  Print("Key: " + keys(i) + ", Value: " + Str(dt.Value(keys(i))))
Next

Print("Dictionary test completed.")