



var x as new XGraphics(64,64,32)
print(str(x.handle))
print(str(x.width) + ", " + str(x.height))


var pp as New XPicture(64,64,32)
print(str(pp.width) + ", " + str(pp.height))
pp.save("./mypic-1.png")


var ff as New XPicture(320,240,32)
ff.load("./scripts/ico.png")
print(str(ff.width) + ", " + str(ff.height))
pp.save("./mypic-2.png")

var c as XGraphics = PIT(pp.Graphics)
print(str(c.width) + ", " + str(c.height))
c.savetofile("mypic-3.png")

Function PIT(g as XGraphics) as XGraphics
	g.clear()
	g.drawline(0,0,54,50)
	return g
End Function
