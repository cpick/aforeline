Vagrant.configure(2) do |config|
  config.vm.box = "ubuntu/bionic64"
  config.vm.synced_folder ".", "/vagrant"

  config.vm.provision "shell", path: "build"
end
