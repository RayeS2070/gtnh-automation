local component = require('component')

for address in component.list('me_interface', true) do
    for _, fluid in ipairs(component.proxy(address).getFluidsInNetwork()) do
        print(fluid.name)
    end
end
