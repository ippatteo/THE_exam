import socket

host = '127.0.0.1'  # Cambiato da 'localhost' a '127.0.0.1'
port = 8080       # Cambiato da 9990 a 8080
address = (host, port)

server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.bind((address))
server_socket.listen(5)
print ("Listening for client . . .")
conn, address = server_socket.accept()
print ("Connected to client at ", address)
while True:
    try:
        output = conn.recv(3001)
        if output:
            print ("Message received from client:")
            print (output)

    except:
        sys.exit(0)
