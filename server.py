import sys
import PyQt5
from pyqtgraph import PlotWidget,plot
import os
import xml.etree.ElementTree as xmlET
import socket
import ctypes

#global settings
oldConnectionsXmlPath="./digiLockPreviousConnections.xml"
TCP_PORT = 420

def connect(Ip):
    print(f"Initializing connection to {Ip}")
    return

class InitialConnectionDialog(PyQt5.QtGui.QDialog):
    def __init__(self, *args, **kwargs):
        super(InitialConnectionDialog, self).__init__(*args, **kwargs)
        
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
        newIp=self.ledit_ip.text()
        print(f"Adding new Ip ({newIp}) to list of old ips")
        root = None
        if(self.oldConXmlTree == None):
            #need to create xmlDocumentFirst
            root = xmlET.Element("ConnectionList")
        else:
            root = self.oldConXmlTree.getroot()
        newElement=xmlET.Element("PreviousConnection")
        newElement.set("IP", newIp)
        root.append(newElement)
        newTree = xmlET.ElementTree(root)
        newTree.write(oldConnectionsXmlPath)
        super(InitialConnectionDialog, self).Ip=(newIp)
        self.accept()
    def on_oldConnectBtn_click(self):
        Ip=self.oldComboBox.currentText()
        super(InitialConnectionDialog, self).Ip(Ip)
        self.accept()
     
    def closeEvent(self, evnt):
        evnt.ignore()
        self.reject()
        

class MainWindow(PyQt5.QtWidgets.QMainWindow):
    def __init__(self, *args, **kwargs):
        super(MainWindow, self).__init__( *args, **kwargs)
        self.setWindowTitle("Digital Locking Server")
        
        self.label_p = PyQt5.QtGui.QLabel("P: ")
        self.ledit_p = PyQt5.QtGui.QLineEdit()
        self.ledit_p.setPlaceholderText("Enter a float")
       
        self.label_i = PyQt5.QtGui.QLabel("I: ")
        self.ledit_i = PyQt5.QtGui.QLineEdit()
        self.ledit_i.setPlaceholderText("Enter a float")
        
        self.label_d = PyQt5.QtGui.QLabel("D: ")
        self.ledit_d = PyQt5.QtGui.QLineEdit()
        self.ledit_d.setPlaceholderText("Enter a float")
        
        self.label_set = PyQt5.QtGui.QLabel("SetP: ")
        self.ledit_set = PyQt5.QtGui.QLineEdit()
        self.ledit_set.setPlaceholderText("Enter a float")
        
        self.send_button = PyQt5.QtGui.QPushButton("Send Values", self)
        
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


    def createTcpSocket():

       
       BUFFER_SIZE = 1024
       MESSAGE = "redpit give plot!"
       
       self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
       self.socket.connect((self.Ip, TCP_PORT))
       self.socket.send(MESSAGE)
       data = s.recv(BUFFER_SIZE)
       

def main():
    app = PyQt5.QtWidgets.QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())

main()

