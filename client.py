import sys
import PyQt5
from pyqtgraph import PlotWidget,plot
import os
import xml.etree.ElementTree as xmlET
import socket
import struct
#import ctypes
from enum import Enum
import time #for sleep
#global settings
oldConnectionsXmlPath="./digiLockPreviousConnections.xml"
TCP_PORT = 4242
TCP_MAX_REC_CHUNK_SIZE = 4096
ADCPRECISION = 10
app=None

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
        self.setWindowTitle("digitalLock Client")
        
        
        
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
        #init plot widget
        self.x_range=range(16384)
        y=[0]*16384
        self.PlotWidget.setBackground('w')
        self.PlotWidget.setLabel('left', 'Ch1 [V]')
        self.PlotWidget.setLabel('bottom', 'n-th sample')
        self.plot=self.PlotWidget.plot(self.x_range,y)
        
        
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
        
        #start network thread
        self.networkingThread_handler = PyQt5.QtCore.QThread()
        self.networkingThread_handler.start()
        
        #place qObject inside this thread
        self.network_thread = NetworkingWorker(self.Ip)
        
        self.network_thread.getGraph_return_signal.connect(self.getGraph_return)
        self.network_thread.getSettings_return_signal.connect(self.getSettings_return)
        self.network_thread.networkTX_start_signal.connect(self.networkTX_start)
        self.network_thread.networkTX_end_signal.connect(self.networkTX_end)
        #important, do this after the QObject has been moved to the new thread
        self.network_thread.moveToThread(self.networkingThread_handler)
        
        #get initial settings from redpitaya
        self.network_thread.getSettings_signal.emit()
        #timer to call getGraph repeadietely
        self.graphUpdateTimer=PyQt5.QtCore.QTimer(self)
        self.graphUpdateTimer.setInterval(1000)
        self.graphUpdateTimer.timeout.connect(self.network_thread.getGraph_signal)
        
        #self.network_thread.getGraph_signal.emit()
        
        self.graphUpdateTimer.start()

    
    @PyQt5.QtCore.pyqtSlot()
    def on_newSendBtn_click(self):
        settings=[]
        for ledit in self.ledit_settings_list:
            settings.append(float(ledit.text()))
        self.network_thread.setSettings_signal.emit(settings)
        
    @PyQt5.QtCore.pyqtSlot(list)
    def getGraph_return(self,list):
        self.plot.setData(self.x_range, list)
        #self.PlotWidget.update()
        app.processEvents()
        
    @PyQt5.QtCore.pyqtSlot(list)
    def getSettings_return(self,list):
        for i in range(len(self.ledit_settings_list)):
            self.ledit_settings_list[i].setText(str(list[i]))
            
    @PyQt5.QtCore.pyqtSlot()
    def networkTX_start(self):
        print("unhandeled networkStart in main")
        
    @PyQt5.QtCore.pyqtSlot()
    def networkTX_end(self):
        print("unhandeled networkEnd in main")
        
    


#see https://stackoverflow.com/questions/20324804/how-to-use-qthread-correctly-in-pyqt-with-movetothread
class NetworkingWorker(PyQt5.QtCore.QObject):
    #emitted signals
    getGraph_signal = PyQt5.QtCore.Signal()
    getSettings_signal = PyQt5.QtCore.Signal()
    setSettings_signal = PyQt5.QtCore.Signal(list)
    getOffset_signal = PyQt5.QtCore.Signal()    
    #consumed signals
    getGraph_return_signal = PyQt5.QtCore.Signal(list)
    getSettings_return_signal = PyQt5.QtCore.Signal(list)
    networkTX_start_signal = PyQt5.QtCore.Signal()
    networkTX_end_signal = PyQt5.QtCore.Signal()
    
    
    def __init__(self,Ip,*args, **kwargs):
        super(NetworkingWorker, self).__init__()
        
        self.Ip=Ip
        
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((self.Ip, TCP_PORT))
       
        self.args = args
        self.kwargs = kwargs
        self.getGraph_signal.connect(self.getGraph)
        self.getSettings_signal.connect(self.getSettings)
        self.setSettings_signal.connect(self.setSettings)
        self.getOffset_signal.connect(self.getOffset)
                
        print("Networking thread initialized")

    @PyQt5.QtCore.pyqtSlot()
    def getGraph(self):
        self.networkTX_start_signal.emit()
        #send request
        type=MessageType.getGraph.value
        message = struct.pack("!ii",type,0)
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
  
        buffer = self.recvall(dataLength)
        if(requestType!=MessageType.getGraph_return.value):
            print("Error unexpected network answer")
        data=struct.unpack("!"+str(dataLength//2)+"h",buffer)
        graphy=[]
        for iter in data:
            graphy.append(iter*(1.0/(2**ADCPRECISION)))
        self.networkTX_end_signal.emit()
        self.getGraph_return_signal.emit(graphy)
    @PyQt5.QtCore.pyqtSlot(list)
    def setSettings(self,list):    
        self.networkTX_start_signal.emit()
        #send settings
        type=MessageType.setSettings.value
        message = b''.join([struct.pack("!ii",type,4*len(list)) , struct.pack("!"+str(len(list))+"f",*list)])
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple=struct.unpack("!ii",buffer)
        requestType=header_tuple[0]
        dataLength =header_tuple[1]
        buffer = self.recvall(dataLength)
        if(buffer!=b""):
            print("Error unexpected network packet after setSettings")
        self.networkTX_end_signal.emit()
    
    def getSettings(self):
        print("getSettings sent")
        self.networkTX_start_signal.emit()
        
        #send request
        type=MessageType.getSettings.value
        message = struct.pack("!ii",type,0)
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
        buffer = self.recvall(dataLength)
        print("getSettings returned data")

        settingslist = list(struct.unpack("!"+str(dataLength//4)+"f",buffer))
        self.networkTX_end_signal.emit()
        #TODO in main be carefull this is an tuple
        print(settingslist)
        self.getSettings_return_signal.emit(settingslist)
        print("getSettings returned signal")

    
    def getOffset(self):
        print("Not implemented yet")
    
    def recvall(self,msg_size):
        arr = bytearray(msg_size)
        pos = 0
        while pos < msg_size:
            if(TCP_MAX_REC_CHUNK_SIZE<msg_size):
                bytesobj = self.socket.recv(TCP_MAX_REC_CHUNK_SIZE)
                actuallyrevievedbytes=len(bytesobj)
                arr[pos:pos+actuallyrevievedbytes]=bytesobj
                pos += actuallyrevievedbytes
            else:
                bytesobj = self.socket.recv(msg_size)
                actuallyrevievedbytes=len(bytesobj)
                arr[pos:pos+actuallyrevievedbytes]=bytesobj
                pos += actuallyrevievedbytes
        return arr

#def main():
app = PyQt5.QtWidgets.QApplication(sys.argv)
window = MainWindow()
window.show()
sys.exit(app.exec_())

#main()

