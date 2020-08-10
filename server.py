import sys
import PyQt5
from pyqtgraph import PlotWidget,plot
import os
import xml.etree.ElementTree as xmlET
import socket
import struct
import ctypes
from enum import Enum
#global settings
oldConnectionsXmlPath="./digiLockPreviousConnections.xml"
TCP_PORT = 4242
TCP_MAX_REC_CHUNK_SIZE = 4096
ADCPRECISION = 10


class MessageType(Enum):
    getGraph=0
    getGraph_return=1
    setSettings=2
    setSettings_return=3
    getSettings=4
    getSettings_return=5
    getOffset=6
    getOffset_return=7
    


def connect(Ip):
    print(f"Initializing connection to {Ip}")
    return

class InitialConnectionDialog(PyQt5.QtGui.QDialog):
    
    def __init__(self, *args, **kwargs):
        super(InitialConnectionDialog, self).__init__(*args, **kwargs)
        self.MainWindow=args[0]
        self.setWindowTitle("New Connection")
        
        #upper row for new connection
        self.label_ip = PyQt5.QtGui.QLabel("Server Address: ")
        self.ledit_ip = PyQt5.QtGui.QLineEdit()
        self.ledit_ip.setPlaceholderText("Please enter valid IP")
        
        self.newConnectBtn = PyQt5.QtGui.QPushButton("New Connection", self)
        self.newConnectBtn.clicked.connect(self.on_newConnectBtn_click)

        self.upperRow = PyQt5.QtGui.QHBoxLayout()
        self.upperRow.addWidget(self.label_ip)
        self.upperRow.addWidget(self.ledit_ip)
        self.upperRow.addWidget(self.newConnectBtn)
        
        self.label_old = PyQt5.QtGui.QLabel("Previous Connections: ")
        self.oldComboBox = PyQt5.QtWidgets.QComboBox()
        self.oldConnectBtn = PyQt5.QtGui.QPushButton("Use Previous", self)
        self.oldConnectBtn.clicked.connect(self.on_oldConnectBtn_click)
        
        self.lowerRow = PyQt5.QtGui.QHBoxLayout()
        self.lowerRow.addWidget(self.label_old)
        self.lowerRow.addWidget(self.oldComboBox)
        self.getOldIpFromXml()
        self.lowerRow.addWidget(self.oldConnectBtn)
        
        self.globalLayout=PyQt5.QtGui.QVBoxLayout()
        self.globalLayout.addLayout(self.upperRow)
        self.globalLayout.addLayout(self.lowerRow)
        
        self.setLayout(self.globalLayout)
        
    @PyQt5.QtCore.pyqtSlot()
    def getOldIpFromXml(self):
        #fill Combo box with old ip's
        self.oldConXmlTree = None
        if(os.path.isfile(oldConnectionsXmlPath)):
            self.oldConXmlTree = xmlET.parse(oldConnectionsXmlPath)
            root = self.oldConXmlTree.getroot()
            for child in root:
                oldIp=child.get("IP")
                self.oldComboBox.addItem(oldIp)
        return
    
    @PyQt5.QtCore.pyqtSlot()
    def on_newConnectBtn_click(self):
        Ip=self.ledit_ip.text()
        print(f"Adding new Ip ({Ip}) to list of old ips")
        root = None
        if(self.oldConXmlTree == None):
            #need to create xmlDocumentFirst
            root = xmlET.Element("ConnectionList")
        else:
            root = self.oldConXmlTree.getroot()
        newElement=xmlET.Element("PreviousConnection")
        newElement.set("IP", Ip)
        root.append(newElement)
        newTree = xmlET.ElementTree(root)
        newTree.write(oldConnectionsXmlPath)
        self.MainWindow.Ip=Ip
        self.accept()
        
    @PyQt5.QtCore.pyqtSlot()
    def on_oldConnectBtn_click(self):
        Ip=self.oldComboBox.currentText()
        self.MainWindow.Ip=Ip
        self.accept()
     
    def closeEvent(self, evnt):
        evnt.ignore()
        self.reject()
        

class MainWindow(PyQt5.QtWidgets.QMainWindow):
    ledit_settings_list=[]
    def __init__(self, *args, **kwargs):
        super(MainWindow, self).__init__( *args, **kwargs)
        self.setWindowTitle("Digital Locking Server")
        
        
        
        self.label_p = PyQt5.QtGui.QLabel("P: ")
        self.ledit_p = PyQt5.QtGui.QLineEdit()
        self.ledit_p.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.ledit_p)
       
        self.label_i = PyQt5.QtGui.QLabel("I: ")
        self.ledit_i = PyQt5.QtGui.QLineEdit()
        self.ledit_i.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.ledit_i)
        
        self.label_d = PyQt5.QtGui.QLabel("D: ")
        self.ledit_d = PyQt5.QtGui.QLineEdit()
        self.ledit_d.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.ledit_d)
        
        self.label_set = PyQt5.QtGui.QLabel("SetP: ")
        self.ledit_set = PyQt5.QtGui.QLineEdit()
        self.ledit_set.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.ledit_set)
        
        self.send_button = PyQt5.QtGui.QPushButton("Send Values", self)
        self.send_button.clicked.connect(self.on_newSendBtn_click)
        
        self.ButtonLayout=PyQt5.QtGui.QHBoxLayout()
        
        self.ButtonLayout.addWidget(self.label_p)
        self.ButtonLayout.addWidget(self.ledit_p)
        self.ButtonLayout.addWidget(self.label_i)
        self.ButtonLayout.addWidget(self.ledit_i)
        self.ButtonLayout.addWidget(self.label_d)
        self.ButtonLayout.addWidget(self.ledit_d)
        self.ButtonLayout.addWidget(self.label_set)
        self.ButtonLayout.addWidget(self.ledit_set)
        self.ButtonLayout.addWidget(self.send_button)
        
        self.PlotWidget = PlotWidget()
  
        self.globalLayout=PyQt5.QtGui.QVBoxLayout()
        self.globalLayout.addWidget(self.PlotWidget)
        self.globalLayout.addLayout(self.ButtonLayout)
        
        self.globalWidget = PyQt5.QtGui.QWidget()
        self.globalWidget.setLayout(self.globalLayout)
        
        self.setCentralWidget(self.globalWidget)
        
        dlg=InitialConnectionDialog(self)
        
        #check if dialog is closed with the close button of the window and terminate the app if this is the case
        if(0 == dlg.exec_()):
            exit()
        else:
            self.createTcpSocket()
        hour = [1,2,3,4,5,6,7,8,9,10]
        temperature = [30,32,34,32,33,31,29,32,35,45]

        # plot data: x, y values
        self.PlotWidget.plot(hour, temperature)
    
    @PyQt5.QtCore.pyqtSlot()
    def on_newSendBtn_click(self):
        sendbuf=[]
        for ledit in self.ledit_settings_list:
            sendbuf.append(float(ledit.text()))
        sendMessage(self.socket,MessageType.setSettings,sendbuf)
        recieveMessage(self.socket,self.ledit_settings_list)
    def createTcpSocket(self):
       self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
       self.socket.connect((self.Ip, TCP_PORT))
       sendMessage(self.socket,MessageType.getSettings,[])
       recieveMessage(self.socket,self.ledit_settings_list)
       

def recvall(socket,msg_size):
    arr = bytearray(msg_size)
    pos = 0
    while pos < msg_size:
        if(TCP_MAX_REC_CHUNK_SIZE<msg_size):
            arr[pos:pos+TCP_MAX_REC_CHUNK_SIZE] = socket.recv(TCP_MAX_REC_CHUNK_SIZE)
        else:
            arr[pos:pos+msg_size] = socket.recv(msg_size)
            pos += msg_size
    return arr
    
def sendMessage(socket,type,data):
    type=type.value
    if(len(data)):
        if(isinstance(data[0], float)):
            message = b''.join([struct.pack("!ii",type,4*len(data)) , struct.pack("!"+"f"*len(data),*data)])
        else:
            message = b''.join([struct.pack("!ii",type,4*len(data)) , struct.pack("!"+"i"*len(data),*data)])
    else:
        message = b''.join([struct.pack("!ii",type,len(data))])
    print(f"sending message {message}, of type {type}.")
    socket.sendall(message)

def recieveMessage(socket,ledit_settings_list):
    buffer = recvall(socket,8)
    header_tuple=struct.unpack("!ii",buffer)
    requestType=header_tuple[0]
    dataLength =header_tuple[1]
    buffer = recvall(socket,dataLength)
    
    if(requestType==MessageType.getSettings_return.value):
        print("Got from pitaya")
        print(buffer)
        buffer_tuple=struct.unpack("!"+"f"*(dataLength//4),buffer)
        for i in range(len(ledit_settings_list)):
            ledit_settings_list[i].setText(str(buffer_tuple[i]))
    elif(requestType==MessageType.getGraph_return.value):
        buffer_tuple=struct.unpack("!"+"i"*(dataLength//4),buffer)
        graphy=[]
        for iter in iter_unpack("!i"):
            graphy.append(iter*(1.0/(2**ADCPRECISION)))
        return graphy
    elif(requestType==MessageType.setSettings_return.value):
        #No return
        pass
    elif(requestType==MessageType.getOffset_return.value):
        buffer_tuple=struct.unpack("!"+"i"*(dataLength//4),buffer)
        pass
    else:
        print(f"Error, invalid MessageType revieved, got rq={requestType},dl={dataLength}")
    
    
def main():
    app = PyQt5.QtWidgets.QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())

main()

