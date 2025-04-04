require 'lowlevel-toolkit'

class Hello
  def say_hello(to, secret)
    to.hello!
  end
end

class Spy
  def hello!
    puts "I was called by #{LowlevelToolkit.who_called_me}"
    puts "Secret is '#{LowlevelToolkit.who_called_me_binding.local_variable_get(:secret)}'"
  end
end

Hello.new.say_hello(Spy.new, 'trustno1')
