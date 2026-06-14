script.Parent = nil
task.spawn(function()
	print("injected")
end)
while true do task.wait(9e9) end