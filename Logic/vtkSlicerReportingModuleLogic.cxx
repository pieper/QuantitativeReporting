/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// ModuleTemplate includes
#include "vtkSlicerReportingModuleLogic.h"

// MRML includes
#include <vtkMRMLAnnotationNode.h>
#include <vtkMRMLAnnotationControlPointsNode.h>
#include <vtkMRMLAnnotationFiducialNode.h>
#include <vtkMRMLAnnotationHierarchyNode.h>
#include <vtkMRMLAnnotationRulerNode.h>
#include <vtkMRMLDisplayableHierarchyNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLReportingReportNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLReportingAnnotationRANONode.h>

// VTK includes
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

// Qt includes
#include <QDomDocument>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtXml>

// CTK includes
#include <ctkDICOMDatabase.h>

// STD includes
#include <cassert>

// DCMTK includes
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/cmdlnarg.h>
#include <dcmtk/ofstd/ofconapp.h>
#include <dcmtk/ofstd/ofstd.h>
#include <dcmtk/ofstd/ofdatime.h>
#include <dcmtk/dcmdata/dcuid.h>         /* for dcmtk version name */
#include <dcmtk/dcmdata/dcdeftag.h>      /* for DCM_StudyInstanceUID */


//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerReportingModuleLogic);

//----------------------------------------------------------------------------
vtkSlicerReportingModuleLogic::vtkSlicerReportingModuleLogic()
{
  this->ActiveReportHierarchyID = NULL;
  this->ActiveMarkupHierarchyID = NULL;
  this->DICOMDatabase = NULL;
}

//----------------------------------------------------------------------------
vtkSlicerReportingModuleLogic::~vtkSlicerReportingModuleLogic()
{
  if (this->ActiveReportHierarchyID)
    {
    delete [] this->ActiveReportHierarchyID;
    this->ActiveReportHierarchyID = NULL;
    }
  if (this->ActiveMarkupHierarchyID)
    {
    delete [] this->ActiveMarkupHierarchyID;
    this->ActiveMarkupHierarchyID = NULL;
    }
}

//----------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Active Report Hierarchy ID = " << (this->GetActiveReportHierarchyID() ? this->GetActiveReportHierarchyID() : "null") << "\n";
  os << indent << "Active Markup Hierarchy ID = " << (this->GetActiveMarkupHierarchyID() ? this->GetActiveMarkupHierarchyID() : "null") << "\n";

}

//---------------------------------------------------------------------------
bool vtkSlicerReportingModuleLogic::InitializeDICOMDatabase()
{
  QSettings settings;
  QString dbPath = settings.value("DatabaseDirectory","").toString();
  std::cout << "Reporting will use database at this location: " << dbPath.toLatin1().data() << std::endl;

  bool success = false;

  if(dbPath != "")
    {
    this->DICOMDatabase = new ctkDICOMDatabase();
    this->DICOMDatabase->openDatabase(dbPath+"/ctkDICOM.sql","Reporting");
    success = this->DICOMDatabase->isOpen();
    //QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    //db.setDatabaseName(dbPath+"/ctkDICOM.sql");
    //success = db.open();
    }
  return success;
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  vtkDebugMacro("SetMRMLSceneInternal");
  
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
// Register all module-specific nodes here
void vtkSlicerReportingModuleLogic::RegisterNodes()
{
  if(!this->GetMRMLScene())
    return;
  vtkMRMLReportingReportNode *rn = vtkMRMLReportingReportNode::New();
  this->GetMRMLScene()->RegisterNodeClass(rn);
  vtkMRMLReportingAnnotationRANONode *rano = vtkMRMLReportingAnnotationRANONode::New();
  this->GetMRMLScene()->RegisterNodeClass(rano);
  rn->Delete();
  rano->Delete();
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::ProcessMRMLNodesEvents(vtkObject *vtkNotUsed(caller),
                                                            unsigned long event,
                                                            void *callData)
{
  vtkDebugMacro("ProcessMRMLNodesEvents");

  vtkMRMLNode* node = reinterpret_cast<vtkMRMLNode*> (callData);
  vtkMRMLAnnotationNode* annotationNode = vtkMRMLAnnotationNode::SafeDownCast(node);
  if (annotationNode)
    {
    switch (event)
      {
      case vtkMRMLScene::NodeAddedEvent:
        this->OnMRMLSceneNodeAdded(annotationNode);
        break;
      case vtkMRMLScene::NodeRemovedEvent:
        this->OnMRMLSceneNodeRemoved(annotationNode);
        break;
      }
    }
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{ 
  if (!node)
    {
    return;
    }
  std::string annotationType;
  if (node->IsA("vtkMRMLAnnotationFiducialNode"))
    {
    annotationType = "Fiducial";
    }
  else if (node->IsA("vtkMRMLAnnotationRulerNode"))
    {
    annotationType = "Ruler";
    }
  else
    {
    return;
    }
  // only want to grab annotation nodes if there's an active markeup
  // hierarchy
  if (!this->GetActiveMarkupHierarchyID())
    {
    return;
    }
  vtkDebugMacro("OnMRMLSceneNodeAdded: active markup hierarchy, got an annotation node added with id " << node->GetID());

  /// make a new hierarchy node to create a parallel tree?
  /// for now, just reasign it
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (hnode)
    {
    hnode->SetParentNodeID(this->GetActiveMarkupHierarchyID());
    }

  // rename it from the reporting node
  vtkMRMLAnnotationNode *annotationNode = vtkMRMLAnnotationNode::SafeDownCast(node);
  vtkMRMLNode *reportNode = NULL;
  vtkMRMLNode *activeReport = this->GetMRMLScene()->GetNodeByID(this->GetActiveReportHierarchyID());
  if (activeReport)
    {
    vtkMRMLDisplayableHierarchyNode *reportHierarchyNode = vtkMRMLDisplayableHierarchyNode::SafeDownCast(activeReport);
    if (reportHierarchyNode)
      {
      reportNode = reportHierarchyNode->GetAssociatedNode();
      }
    }
  if (annotationNode && reportNode)
    {
    std::string annotationName = std::string(reportNode->GetDescription())+"_"+annotationType;
    annotationNode->SetName(annotationNode->GetScene()->GetUniqueNameByString(annotationName.c_str()));
    }
  
  // TODO: sanity check to make sure that the annotation's AssociatedNodeID
  // attribute points to the current volume
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::OnMRMLSceneNodeRemoved(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
const char *vtkSlicerReportingModuleLogic::GetSliceUIDFromMarkUp(vtkMRMLAnnotationNode *node)
{
  std::string UID = "NONE";

  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: no input node!");
    return  UID.c_str();
    }

  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: No MRML Scene defined!");
    return UID.c_str();
    }
  
  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(node);
  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Input node is not a control points node!");
    return UID.c_str();
    }
  
  int numPoints = cpNode->GetNumberOfControlPoints();
  vtkDebugMacro("GetSliceUIDFromMarkUp: have a control points node with " << numPoints << " points");

  // get the associated node
  const char *associatedNodeID = cpNode->GetAttribute("AssociatedNodeID");
  if (!associatedNodeID)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: No AssociatedNodeID on the annotation node");
    return UID.c_str();
    }
  vtkMRMLScalarVolumeNode *volumeNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(associatedNodeID);
  if (!mrmlNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node not found by id: " << associatedNodeID);
    return UID.c_str();
    }
  volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlNode);
  if (!volumeNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node with id: " << associatedNodeID << " is not a volume node!");
    return UID.c_str();
    }

  // get the list of UIDs from the volume
  if (!volumeNode->GetAttribute("DICOM.instanceUIDs"))
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Volume node with id: " << associatedNodeID << " doesn't have a list of UIDs under the attribute DICOM.instanceUIDs!");
    return UID.c_str();
    }
  std::string uidsString = volumeNode->GetAttribute("DICOM.instanceUIDs");
  // break them up into a vector, they're space separated
  std::vector<std::string> uidVector;
  char *uids = new char[uidsString.size()+1];
  strcpy(uids,uidsString.c_str());
  char *ptr;
  ptr = strtok(uids, " ");
  while (ptr != NULL)
    {
    vtkDebugMacro("Parsing UID = " << ptr);
    uidVector.push_back(std::string(ptr));
    ptr = strtok(NULL, " ");
    }
  
  // get the RAS to IJK matrix from the volume
  vtkSmartPointer<vtkMatrix4x4> ras2ijk = vtkSmartPointer<vtkMatrix4x4>::New();
  volumeNode->GetRASToIJKMatrix(ras2ijk);


//  for (int i = 0; i < numPoints; i++)
  int i = 0;
    {
    vtkDebugMacro("i " << " uid = " << uidVector[i].c_str());
    // get the RAS point
    double ras[4] = {0.0, 0.0, 0.0, 1.0};
    cpNode->GetControlPointWorldCoordinates(i, ras);
    // convert point from ras to ijk
    double ijk[4] = {0.0, 0.0, 0.0, 1.0};
    ras2ijk->MultiplyPoint(ras, ijk);
    vtkDebugMacro("Point " << i << " ras = " << ras[0] << ", " << ras[1] << ", " << ras[2] << " converted to ijk  = " << ijk[0] << ", " << ijk[1] << ", " << ijk[2] << ", getting uid at index " << ijk[2] << " (uid vector size = " << uidVector.size() << ")");
    if (uidVector.size() > ijk[2])
      {
      // assumption is that the dicom UIDs are in order by k
      UID = uidVector[ijk[2]];
      }
    else
      {
      // multiframe data? set UID to the first one, but will need to store the
      // frame number on AIM import
      UID = uidVector[0];
      }
    }  
  return UID.c_str();

}
//---------------------------------------------------------------------------
char *vtkSlicerReportingModuleLogic::GetTopLevelHierarchyNodeID()
{
  if (this->GetMRMLScene() == NULL)
    {
    return NULL;
    }
  const char *topLevelName = "Reporting Hierarchy";

  /// check for a top level hierarchy
  if (!this->GetMRMLScene()->GetFirstNodeByName(topLevelName))
    {
    vtkMRMLDisplayableHierarchyNode *reportingHierarchy = vtkMRMLDisplayableHierarchyNode::New();
    reportingHierarchy->HideFromEditorsOff();
    reportingHierarchy->SetName(topLevelName);
    this->GetMRMLScene()->AddNode(reportingHierarchy);
    reportingHierarchy->Delete();
    }
   
  char *toplevelNodeID =  this->GetMRMLScene()->GetFirstNodeByName(topLevelName)->GetID();;

  return toplevelNodeID;
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::InitializeHierarchyForReport(vtkMRMLReportingReportNode *node)
{
  if (!node)
    {
    vtkErrorMacro("InitializeHierarchyForReport: null input report");
    return;
    }

  if (!node->GetScene() || !node->GetID())
    {
    vtkErrorMacro("InitializeHierarchyForReport: No MRML Scene defined on node, or else it doesn't have an id");
    return;
    }

  vtkDebugMacro("InitializeHierarchyForReport: setting up hierarchy for report " << node->GetID());

  /// does the node already have a hierarchy set up for it?
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (hnode)
    {
    vtkDebugMacro("InitializeHierarchyForReport: report " << node->GetID() << " already has a hierarchy associated with it, " << hnode->GetID());
    /// make the report hierarchy active 
    this->SetActiveReportHierarchyID(hnode->GetID());
    return;
    }
    /// otherwise, create a 1:1 hierarchy for this node
  vtkMRMLDisplayableHierarchyNode *reportHierarchyNode = vtkMRMLDisplayableHierarchyNode::New();
  /// it's a stealth node:
  reportHierarchyNode->HideFromEditorsOn();
  std::string hnodeName = std::string(node->GetName()) + std::string(" Hierarchy");
  reportHierarchyNode->SetName(this->GetMRMLScene()->GetUniqueNameByString(hnodeName.c_str()));
  this->GetMRMLScene()->AddNode(reportHierarchyNode);

  
  // make it the child of the top level reporting node
  const char *topLevelID = this->GetTopLevelHierarchyNodeID();
  vtkDebugMacro("InitializeHierarchyForReport: pointing report hierarchy node at top level id " << (topLevelID ? topLevelID : "null"));
  reportHierarchyNode->SetParentNodeID(topLevelID);
  
  // set the displayable node id to point to this report node
  node->SetDisableModifiedEvent(1);
  reportHierarchyNode->SetDisplayableNodeID(node->GetID());
  node->SetDisableModifiedEvent(0);

  /// make the report hierarchy active 
  this->SetActiveReportHierarchyID(reportHierarchyNode->GetID());
  vtkDebugMacro("Set the active report hierarchy id = " << (reportHierarchyNode->GetID() ? reportHierarchyNode->GetID() : "null"));

  /// create an annotation node with hierarchy
  vtkMRMLHierarchyNode *ranoHierarchyNode = vtkMRMLHierarchyNode::New();
  /// it's a stealth node:
  ranoHierarchyNode->HideFromEditorsOn();
  std::string ranohnodeName = std::string(node->GetName()) + std::string(" RANO Hierarchy");
  ranoHierarchyNode->SetName(this->GetMRMLScene()->GetUniqueNameByString(ranohnodeName.c_str()));
  this->GetMRMLScene()->AddNode(ranoHierarchyNode);
  // make it the child of the report node
  ranoHierarchyNode->SetParentNodeID(reportHierarchyNode->GetID());
  
  vtkMRMLReportingAnnotationRANONode *ranoNode = vtkMRMLReportingAnnotationRANONode::New();
  this->GetMRMLScene()->AddNode(ranoNode);
  ranoHierarchyNode->SetAssociatedNodeID(ranoNode->GetID());
  
  /// clean up
  ranoNode->Delete();
  ranoHierarchyNode->Delete();
  reportHierarchyNode->Delete();
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::InitializeHierarchyForVolume(vtkMRMLVolumeNode *node)
{
  if (!node)
    {
    vtkErrorMacro("InitializeHierarchyForVolume: null input volume");
    return;
    }

  if (!node->GetScene() || !node->GetID())
    {
    vtkErrorMacro("InitializeHierarchyForVolume: No MRML Scene defined on node, or else it doesn't have an id");
    return;
    }

  vtkDebugMacro("InitializeHierarchyForVolume: setting up hierarchy for volume " << node->GetID());

  /// does the node already have a hierarchy set up for it?
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  char * volumeHierarchyNodeID = NULL;
  if (hnode)
    {
    vtkDebugMacro("InitializeHierarchyForVolume: volume " << node->GetID() << " already has a hierarchy associated with it, " << hnode->GetID() << ", making it a child of " << (this->GetActiveReportHierarchyID() ? this->GetActiveReportHierarchyID() : "null"));
    volumeHierarchyNodeID = hnode->GetID();
    // make sure it's a child of the report
    hnode->SetParentNodeID(this->GetActiveReportHierarchyID());
    }
  else
    {
    /// otherwise, create a 1:1 hierarchy for this node
    vtkMRMLDisplayableHierarchyNode *volumeHierarchyNode = vtkMRMLDisplayableHierarchyNode::New();
    /// it's a stealth node:
    volumeHierarchyNode->HideFromEditorsOn();
    std::string hnodeName = std::string(node->GetName()) + std::string(" Hierarchy ");
    volumeHierarchyNode->SetName(this->GetMRMLScene()->GetUniqueNameByString(hnodeName.c_str()));
    this->GetMRMLScene()->AddNode(volumeHierarchyNode);
    volumeHierarchyNodeID = volumeHierarchyNode->GetID();
    
    // make it the child of the active report node
    if (!this->GetActiveReportHierarchyID())
      {
      vtkWarningMacro("No active report, please select one!");
      }
    else
      {
      vtkDebugMacro("Set volume hierarchy parent to active report id " << this->GetActiveReportHierarchyID());
      }
    volumeHierarchyNode->SetParentNodeID(this->GetActiveReportHierarchyID());
    
    // set the displayable node id to point to this volume node
    node->SetDisableModifiedEvent(1);
    volumeHierarchyNode->SetDisplayableNodeID(node->GetID());
    node->SetDisableModifiedEvent(0);
    /// clean up
    volumeHierarchyNode->Delete();
    }
  
  /// add an annotations hierarchy if it doesn't exist
  std::string ahnodeName = std::string("Markup ") + std::string(node->GetName());
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetFirstNodeByName(ahnodeName.c_str());
  char *ahnodeID = NULL;
  if (!mrmlNode)
    {
    vtkMRMLAnnotationHierarchyNode *ahnode = vtkMRMLAnnotationHierarchyNode::New();
    ahnode->HideFromEditorsOff();
    ahnode->SetName(ahnodeName.c_str());
    this->GetMRMLScene()->AddNode(ahnode);
    ahnodeID = ahnode->GetID();
    // make it a child of the volume
    vtkDebugMacro("Setting annotation markup hierarchy's parent to volume hierarchy id " << volumeHierarchyNodeID);
    //this->GetMRMLScene()->GetNodeByID(volumeHierarchyNodeID)->SetDisableModifiedEvent(1);
    ahnode->SetDisableModifiedEvent(1);
    ahnode->SetParentNodeID(volumeHierarchyNodeID);
    //this->GetMRMLScene()->GetNodeByID(volumeHierarchyNodeID)->SetDisableModifiedEvent(0);
    ahnode->SetDisableModifiedEvent(0);
    ahnode->Delete();
    }
  else
    {
    ahnodeID = mrmlNode->GetID();
    }
  /// make the annotation hierarchy active so new ones will get added to it
  this->SetActiveMarkupHierarchyID(ahnodeID);
  vtkDebugMacro("Set the active markup hierarchy id from node id = " << (ahnodeID ? ahnodeID : "null"));
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::SetActiveMarkupHierarchyIDFromNode(vtkMRMLNode *node)
{
  if (!node || !node->GetName())
    {
    vtkDebugMacro("SetActiveMarkupHierarchyIDFromNode: node is " << (node? "not null" : "null") << ", name is " << (node->GetName() ? node->GetName() : "null") << ", settting active id to null");
    this->SetActiveMarkupHierarchyID(NULL);
    return;
    }

  // look for a markup node associated with this node
  std::string ahnodeName = std::string("Markup ") + std::string(node->GetName());
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetFirstNodeByName(ahnodeName.c_str());
                                                                   
  if (!mrmlNode)
    {
    vtkDebugMacro("SetActiveMarkupHierarchyIDFromNode: didn't find markup node by name " << ahnodeName.c_str() << ", trying to find it in the volume's hierarchy");
    // get the hierarchy node associated with this node
    vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
    if (hnode)
      {
      // get the first level children, one should be a markup annotation
      // hierarchy
      std::vector< vtkMRMLHierarchyNode* > children = hnode->GetChildrenNodes();
      for (unsigned int i = 0; i < children.size(); ++i)
        {
        if (children[i]->IsA("vtkMRMLAnnotationHierarchyNode") &&
            strncmp(children[i]->GetName(), "Markup", strlen("Markup")) == 0)
          {
          vtkDebugMacro("Found an annotation hierarchy node with a name that starts with Markup under this volume, using active markup hierarchy id " << children[i]->GetID());
          this->SetActiveMarkupHierarchyID(children[i]->GetID());
          return;
          }
        }
      }
    if (!mrmlNode)
      {
      vtkWarningMacro("SetActiveMarkupHierarchyIDFromNode: didn't find markup node in volume hierarchy, setting active hierarchy to null");
      this->SetActiveMarkupHierarchyID(NULL);
      return;
      }
    }
  vtkDebugMacro("SetActiveMarkupHierarchyIDFromNode: Setting active markup hierarchy to " << mrmlNode->GetID());
  this->SetActiveMarkupHierarchyID(mrmlNode->GetID());
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::SetActiveMarkupHierarchyIDToNull()
{
  if (this->ActiveMarkupHierarchyID)
    {
    delete [] this->ActiveMarkupHierarchyID;
    }
  this->ActiveMarkupHierarchyID = NULL;
}

//---------------------------------------------------------------------------
char *vtkSlicerReportingModuleLogic::GetVolumeIDForReportNode(vtkMRMLReportingReportNode *node)
{
  if (!node)
    {
    return NULL;
    }
  // get the associated hierarchy node for this report
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (!hnode)
    {
    vtkErrorMacro("GetVolumeIDForReportNode: no associated hierarchy node for reporting node " << node->GetID());
    return NULL;
    }
  char *volumeID = NULL;
  // get the children and look for the first volume node
  std::vector< vtkMRMLHierarchyNode *> allChildren;
  hnode->GetAllChildrenNodes(allChildren);
  for (unsigned int i = 0; i < allChildren.size(); ++i)
    {
    vtkMRMLNode *mrmlNode = allChildren[i]->GetAssociatedNode();
    if (mrmlNode)
      {
      if (mrmlNode->IsA("vtkMRMLVolumeNode"))
        {      
        volumeID = mrmlNode->GetID();
        return volumeID;
        }
      }
    }
  
  return volumeID;
}

//---------------------------------------------------------------------------
char *vtkSlicerReportingModuleLogic::GetAnnotationIDForReportNode(vtkMRMLReportingReportNode *node)
{

  vtkErrorMacro("GetAnnotationIDForReportNode: This method is deprecated! Should not be here!");
  assert(0);


  if (!node)
    {
    return NULL;
    }
  // get the associated hierarchy node for this report
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (!hnode)
    {
    vtkErrorMacro("GetAnnotationIDForReportNode: no associated hierarchy node for reporting node " << node->GetID());
    return NULL;
    }
  char *annotationID = NULL;
  // get the children and look for the first annotation node
  std::vector< vtkMRMLHierarchyNode *> allChildren;
  hnode->GetAllChildrenNodes(allChildren);
  for (unsigned int i = 0; i < allChildren.size(); ++i)
    {
    vtkMRMLNode *mrmlNode = allChildren[i]->GetAssociatedNode();
    if (mrmlNode)
      {
      // TODO: check for a superclass?
      if (mrmlNode->IsA("vtkMRMLReportingAnnotationRANONode"))
        {      
        annotationID = mrmlNode->GetID();
        return annotationID;
        }
      }
    }
  
  return annotationID;
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::HideAnnotationsForOtherReports(vtkMRMLReportingReportNode *node)
{
  if (!node)
    {
    return;
    }
  // get the top level reporting module hierarchy
  char *topNodeID = this->GetTopLevelHierarchyNodeID();
  if (!topNodeID)
    {
    return;
    }
  vtkMRMLNode *topNode = this->GetMRMLScene()->GetNodeByID(topNodeID);
  if (!topNode)
    {
    return;
    }
  vtkMRMLHierarchyNode *topHierarchyNode =  vtkMRMLHierarchyNode::SafeDownCast(topNode);
  if (!topHierarchyNode)
    {
    vtkErrorMacro("HideAnnotationsForOtherReports: error casting top node with id " << topNodeID << " to a mrml hierarchy node");
    return;
    }
  // get the associated hierarchy node for this report
  vtkMRMLHierarchyNode *thisReportHierarchyNode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (!thisReportHierarchyNode)
    {
    vtkErrorMacro("HideAnnotationsForOtherReports: no  hierarchy node for report node " << node->GetID());
    return;
    }
  // get the children reporting nodes immediately under the top hierarchy node
  std::vector< vtkMRMLHierarchyNode* > children = topHierarchyNode->GetChildrenNodes();
  for (unsigned int i = 0; i < children.size(); ++i)
    {
    int visibFlag = 0;
    // if it's this report hierarchy node, need to turn on annotations
    if (strcmp(thisReportHierarchyNode->GetID(), children[i]->GetID()) == 0)
      {
      // turn on annotations
      visibFlag = 1;
      }
    // get all the children of this report
    std::vector< vtkMRMLHierarchyNode *> allChildren;
    children[i]->GetAllChildrenNodes(allChildren);
    for (unsigned int j = 0; j < allChildren.size(); ++j)
      {
      vtkMRMLNode *mrmlNode = allChildren[j]->GetAssociatedNode();
      if (mrmlNode && mrmlNode->GetID() && mrmlNode->IsA("vtkMRMLAnnotationNode"))
        {
        vtkDebugMacro("HideAnnotationsForOtherReports: Found an annotation node " << mrmlNode->GetID() << ", visib flag = " << visibFlag);
        // get it's display node
        vtkMRMLAnnotationNode *annotationNode = vtkMRMLAnnotationNode::SafeDownCast(mrmlNode);
        if (!annotationNode)
          {
          vtkErrorMacro("HideAnnotationsForOtherReports: unalbe to convert associated node to an annotation node, at " << mrmlNode->GetID());
          return;
          }
        annotationNode->SetVisible(visibFlag);
        int numDisplayNodes = annotationNode->GetNumberOfDisplayNodes();
        for (int n = 0; n < numDisplayNodes; ++n)
          {
          vtkMRMLDisplayNode *displayNode = annotationNode->GetNthDisplayNode(n);
          if (displayNode)
            {
            vtkDebugMacro("HideAnnotationsForOtherReports: Setting display node " << displayNode->GetID() << " visibility");
            displayNode->SetVisibility(visibFlag);
            }
          }
        }
          
      }
    }
}

int vtkSlicerReportingModuleLogic::SaveReportToAIM(vtkMRMLReportingReportNode *reportNode, const char *filename)
{
  if(!this->DICOMDatabase)
    {
    vtkErrorMacro("SaveReportToAIM: DICOM database not initialized!");
    return EXIT_FAILURE;
    }

  if (!reportNode)
    {
    vtkErrorMacro("SaveReportToAIM: no report node given.");
    return EXIT_FAILURE;
    }
  
  if (!filename)
    {
    vtkErrorMacro("SaveReportToAIM: no file name given.");
    return EXIT_FAILURE;
    }

  vtkDebugMacro("SaveReportToAIM: file name = " << filename);

  vtkMRMLScalarVolumeNode *volumeNode = NULL;
  vtkMRMLAnnotationHierarchyNode *markupHierarchyNode = NULL;

  // only one volume is allowed for now, so get the active one
  char *volumeID = this->GetVolumeIDForReportNode(reportNode);
  if (volumeID)
    {
    vtkMRMLNode *mrmlVolumeNode = this->GetMRMLScene()->GetNodeByID(volumeID);
    if (!mrmlVolumeNode)
      {
      vtkErrorMacro("SaveReportToAIM: volume node not found by id: " << volumeID);
      }
    else
      {
      volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlVolumeNode);
      }
    }
  if (volumeNode)
    {
    // set this volume's markup hierarchy to be active, just to make sure
    this->SetActiveMarkupHierarchyIDFromNode(volumeNode);
    // now get it
    const char *markupID = this->GetActiveMarkupHierarchyID();
    vtkMRMLNode *mrmlMarkupNode = this->GetMRMLScene()->GetNodeByID(markupID);
    if (mrmlMarkupNode)
      {
      markupHierarchyNode = vtkMRMLAnnotationHierarchyNode::SafeDownCast(mrmlMarkupNode);
      if(!markupHierarchyNode)
      {
        std::cerr << "ERROR: markupHierarchyNode not found!" << std::endl;
        return EXIT_FAILURE;
      }
      }
    }

  // open the file for writing
  
  // generated the document and parent elements
  //
  // (Step 1) Initialize ImageAnnotation and attributes
  QDomDocument doc;
  QDomProcessingInstruction xmlDecl = doc.createProcessingInstruction("xml","version=\"1.0\"");
  doc.appendChild(xmlDecl);

  QDomElement root = doc.createElement("ImageAnnotation");
  root.setAttribute("xmlns","gme://caCORE.caCORE/3.2/edu.northwestern.radiology.AIM");
  root.setAttribute("aimVersion","3.0");
  root.setAttribute("cagridId","0");

  root.setAttribute("codeMeaning","Response Assessment in Neuro-Oncology");
  root.setAttribute("codeValue", "RANO");
  root.setAttribute("codeSchemeDesignator", "RANO");
  root.setAttribute("dateTime","2012-02-29T00:00:00");
  root.setAttribute("name",reportNode->GetDescription());
  root.setAttribute("uniqueIdentifier","n.a");
  root.setAttribute("xmlns:xsi","http://www.w3.org/2001/XMLSchema-instance");
  root.setAttribute("xsi:schemaLocation","gme://caCORE.caCORE/3.2/edu.northwestern.radiology.AIM AIM_v3_rv11_XML.xsd");
  
  doc.appendChild(root);

  // (Step 2) Create inference collection and initialize each of the inference
  // objects based on the content of the annotation node
  //
  // Deprecated
  
  // (Step 3) Initialize user/equipment/person (these have no meaning for now
  // here)
  QDomElement user = doc.createElement("user");
  user.setAttribute("cagridId","0");
  user.setAttribute("loginName","slicer");
  user.setAttribute("name","slicer");
  user.setAttribute("numberWithinRoleOfClinicalTrial","1");
  user.setAttribute("roleInTrial","Performing");
  root.appendChild(user);

  QDomElement equipment = doc.createElement("equipment");
  equipment.setAttribute("cagridId","0");
  equipment.setAttribute("manufacturerModelName","3D_Slicer_4_Reporting");
  equipment.setAttribute("manufacturerName","Brigham and Women's Hospital");
  equipment.setAttribute("softwareVersion","0.0.1");
  root.appendChild(equipment);

  QDomElement person = doc.createElement("person");
  person.setAttribute("birthDate","1990-01-01T00:00:00");
  person.setAttribute("cagridId","0");
  person.setAttribute("id","123456");
  person.setAttribute("name","Anonymous");
  person.setAttribute("sex","M");
  root.appendChild(person);

  // (Step 4) Go over the markup elements and add them to the geometric shape
  // collection. Here we might want to keep track of the volume being
  // referenced, but since one AIM file is for one volume, we don't really
  // need to do this.
  QDomElement gsc = doc.createElement("geometricShapeCollection");
  root.appendChild(gsc);

  // and now print!
  
  // print out the report
  if (reportNode)
    {
    std::cout << "SaveReportToAIM: saving report node " << reportNode->GetName() << std::endl;
    }
  
  // print out the volume
  if (volumeNode)
    {
    std::cout << "SaveReportToAIM: saving volume node " << volumeNode->GetName() << std::endl;
    }
  
  // print out the markups
  //   keep the list of referenced slice UIDs so that they can be saved in the
  //   final step
  QStringList allInstanceUIDs;
  int shapeId = 0;
  if (markupHierarchyNode)
    {
    // get all the hierarchy nodes under the mark up node
    std::vector< vtkMRMLHierarchyNode *> allChildren;
    markupHierarchyNode->GetAllChildrenNodes(allChildren);
    // get the associated markups and print them
    for (unsigned int i = 0; i < allChildren.size(); ++i)
      {
      vtkMRMLNode *mrmlAssociatedNode = allChildren[i]->GetAssociatedNode();
      if (mrmlAssociatedNode)
        {
        vtkMRMLAnnotationNode *annNode = vtkMRMLAnnotationNode::SafeDownCast(mrmlAssociatedNode);
        // print out a point
        vtkMRMLAnnotationFiducialNode *fidNode = vtkMRMLAnnotationFiducialNode::SafeDownCast(mrmlAssociatedNode);          
        vtkMRMLAnnotationRulerNode *rulerNode = vtkMRMLAnnotationRulerNode::SafeDownCast(mrmlAssociatedNode);

        if(fidNode || rulerNode)
          {
          // TODO: need to handle the case of multiframe data .. ?
          QString sliceUID = this->GetSliceUIDFromMarkUp(annNode);

          QStringList sliceUIDList;
          sliceUIDList << sliceUID;
          allInstanceUIDs << sliceUID;

          if(sliceUID == "NONE")
            {
            std::cout << "Cannot save AIM report: volumes being annotated are not DICOM volumes!";
            return EXIT_FAILURE;
            }

          QStringList coordStr = this->GetMarkupPointCoordinatesStr(annNode);

          QDomElement gs = doc.createElement("GeometricShape");

          // GeometricShape markup-specific initialization

          // Fiducial = AIM Point
          if (fidNode)
            {
            std::cerr << "SaveReportToAIM: saving Point from node named " << fidNode->GetName() << std::endl;

            if(coordStr.size()!=2)
              {
              vtkErrorMacro("Failed to obtain fiducial points for markup point!");
              return EXIT_FAILURE;
              }

            gs.setAttribute("xsi:type","Point");
            gs.setAttribute("shapeIdentifier",shapeId++);
            gs.setAttribute("includeFlag", "true");
            gs.setAttribute("cagridId","0");
            }
          
          // Ruler = AIM MultiPoint
          if (rulerNode)
            {
            std::cerr << "SaveReportToAIM: saving MultiPoint from node named " << rulerNode->GetName() << std::endl;

            if(coordStr.size()!=4)
              {
              vtkErrorMacro("Failed to obtain fiducial points for markup point!");
              return EXIT_FAILURE;
              }

            gs.setAttribute("xsi:type","MultiPoint");
            gs.setAttribute("shapeIdentifier",shapeId++);
            gs.setAttribute("includeFlag", "true");
            gs.setAttribute("cagridId","0");
            }
 
          // Procedure for saving the list of points should be the same for
          // all markup elements
          this->AddSpatialCoordinateCollectionElement(doc, gs, coordStr, sliceUIDList);
          gsc.appendChild(gs);
        }
      else
        {
        vtkWarningMacro("SaveReportToAIM: unsupported markup type, of class: " << mrmlAssociatedNode->GetClassName());
        }
      }
    }
  }

  // (Step 5) Iterate over referenced volume UIDs and add to the report
  // imageReferenceCollection
  //  +-ImageReference
  //     +-imageStudy
  //        +-ImageStudy (why do they have this nesting?)
  //           +-imageSeries
  //              +-ImageSeries
  //                 +-imageCollection
  //                    +-Image -- whooh ...

  // iterate over all instance UIDs and find all the series and corresponding
  // study/series UIDs referenced by the markups we have
  std::map<QString, QStringList> seriesToImageList;  // seriesUID to the list of unique imageUIDs
  std::map<QString, QStringList> studyToSeriesList;  // studyUID to the list of unique seriesUIDs
  for(int i=0;i<allInstanceUIDs.size();i++)
    {
    // query db only for the first UID in the list, since they should all
    // belong to the same series
    this->DICOMDatabase->loadInstanceHeader(allInstanceUIDs[i].toLatin1().data());
    QString imageUID = this->DICOMDatabase->headerValue("0008,0018");
    QString studyUID = this->DICOMDatabase->headerValue("0020,000d");
    QString seriesUID = this->DICOMDatabase->headerValue("0020,000e");
    QString classUID = QString("uninitialized");
//    QString classUID = this->DICOMDatabase->headerValue("0008,0016");
//    TODO: classUID is not stored correctly in the database for now, skip it
    // AF: why not keep the actual values in the database?

    std::cout << "imageUID = " << imageUID.toLatin1().data() << std::endl;
    std::cout << "studyUID = " << studyUID.toLatin1().data() << std::endl;
    std::cout << "seriesUID = " << seriesUID.toLatin1().data() << std::endl;
//   std::cout << "sclassUID = " << classUID.toLatin1().data() << std::endl;

    imageUID = imageUID.split("]")[0].split("[")[1];
    studyUID = studyUID.split("]")[0].split("[")[1];
    seriesUID = seriesUID.split("]")[0].split("[")[1];

    if(seriesToImageList.find(seriesUID) == seriesToImageList.end())
      seriesToImageList[seriesUID] = QStringList() << imageUID;
    else
      if(seriesToImageList[seriesUID].indexOf(imageUID) == -1)
        seriesToImageList[seriesUID] << imageUID;
    
    if(studyToSeriesList.find(studyUID) == seriesToImageList.end())
      studyToSeriesList[studyUID] = QStringList() << seriesUID;
    else
      if(studyToSeriesList[studyUID].indexOf(seriesUID) == -1)
        studyToSeriesList[studyUID] << seriesUID;
    }

  QDomElement irc = doc.createElement("imageReferenceCollection");
  root.appendChild(irc);

  //for(std::vector<QStringList>::const_iterator it=volumeUIDLists.begin();
  //  it!=volumeUIDLists.end();++it)
  //  {
  for(std::map<QString,QStringList>::const_iterator mIt=studyToSeriesList.begin();
    mIt!=studyToSeriesList.end();++mIt)
    {

    QString studyUID = mIt->first;
    QStringList seriesUIDs = mIt->second;

    for(int ser=0;ser<seriesUIDs.size();++ser)
      {

      QString seriesUID = seriesUIDs[ser];

      // for each list, create a new ImageReference element
      QDomElement ir = doc.createElement("ImageReference");
      ir.setAttribute("cagridId","0");
      ir.setAttribute("xsi:type","DICOMImageReference");
      irc.appendChild(ir);

      QDomElement study = doc.createElement("imageStudy");
      ir.appendChild(study);

      QDomElement study1 = doc.createElement("ImageStudy");
      study1.setAttribute("cagridId","0");
      study1.setAttribute("instanceUID",studyUID.toLatin1().data());
      study1.setAttribute("startDate","2000-01-01T00:00:00");
      study1.setAttribute("startTime","000000");
      study.appendChild(study1);

      // 
      QDomElement series = doc.createElement("imageSeries");
      study1.appendChild(series);

      QDomElement series1 = doc.createElement("ImageSeries");
      series1.setAttribute("cagridId","0");
      series1.setAttribute("instanceUID",seriesUID.toLatin1().data());
      series.appendChild(series1);

      QDomElement ic = doc.createElement("imageCollection");
      series.appendChild(ic);

      QStringList uidList = seriesToImageList[seriesUID];

      for(int i=0;i<uidList.size();i++)
      {
        QDomElement image = doc.createElement("Image");
        image.setAttribute("cagridId","0");
        image.setAttribute("sopClassUID","NA"); // FIXME
        image.setAttribute("sopInstanceUID",uidList[i]);
        ic.appendChild(image);
      }
    }
  }

  // close the file
  
  std::cout << "Here comes the AIM: " << std::endl;
  QString xml = doc.toString();
  std::cout << qPrintable(xml);

  std::ofstream outputFile(filename);
  outputFile << qPrintable(xml);

  return EXIT_SUCCESS;
    
}

int vtkSlicerReportingModuleLogic::AddSpatialCoordinateCollectionElement(QDomDocument &doc, QDomElement &parent,
  QStringList &coordList, QStringList &sliceUIDList)
{
  QDomElement fidscC = doc.createElement("spatialCoordinateCollection");
  parent.appendChild(fidscC);

  // All points should have the same slice UID, because coordinates are
  // defined on the slice
  //if(coordList.size()/2 != sliceUIDList.size())
  //  return EXIT_FAILURE;

  for(int i=0;i<coordList.size();i+=2)
    {
    QDomElement sc = doc.createElement("SpatialCoordinate");
    fidscC.appendChild(sc);

    sc.setAttribute("cagridId","0");
    sc.setAttribute("coordinateIndex","0");
    sc.setAttribute("imageReferenceUID",sliceUIDList[0]);
    sc.setAttribute("referenceFrameNumber","1"); // TODO: maybe add handling of multiframe DICOM?
    sc.setAttribute("xsi:type", "TwoDimensionSpatialCoordinate");
    sc.setAttribute("x", coordList[i]);
    sc.setAttribute("y", coordList[i+1]);
    }

  return EXIT_SUCCESS;
}

vtkMRMLScalarVolumeNode* vtkSlicerReportingModuleLogic::GetMarkupVolumeNode(vtkMRMLAnnotationNode *node)
{
  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: no input node!");
    return  0;
    }

  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: No MRML Scene defined!");
    return 0;
    }

  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(node);
  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Input node is not a control points node!");
    return 0;
    }

  int numPoints = cpNode->GetNumberOfControlPoints();
  vtkDebugMacro("GetSliceUIDFromMarkUp: have a control points node with " << numPoints << " points");

  // get the associated node
  const char *associatedNodeID = cpNode->GetAttribute("AssociatedNodeID");
  if (!associatedNodeID)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: No AssociatedNodeID on the annotation node");
    return 0;
    }
  vtkMRMLScalarVolumeNode *volumeNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(associatedNodeID);
  if (!mrmlNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node not found by id: " << associatedNodeID);
    return 0;
    }
  volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlNode);
  if (!volumeNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node with id: " << associatedNodeID << " is not a volume node!");
    return 0;
    }
  std::cout << "Associated volume node ID: " << volumeNode->GetID() << std::endl;
  if (this->GetDebug())
    {
    vtkIndent ind;
    volumeNode->PrintSelf(std::cout,ind);
    }
  return volumeNode;
}

QStringList vtkSlicerReportingModuleLogic::GetMarkupPointCoordinatesStr(vtkMRMLAnnotationNode *ann)
{
  QStringList sl;
  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(ann);
  if (!cpNode)
    {
    vtkErrorMacro("GetMarkupPointCoordinatesStr: Input node is not a control points node!");
    return sl;
    }

  int numPoints = cpNode->GetNumberOfControlPoints();

  vtkMRMLScalarVolumeNode *vol = this->GetMarkupVolumeNode(ann);
  if(!vol)
  {
    vtkErrorMacro("Failed to obtain volume pointer!");
    return sl;
  }
  vtkSmartPointer<vtkMatrix4x4> ras2ijk = vtkSmartPointer<vtkMatrix4x4>::New();
  vol->GetRASToIJKMatrix(ras2ijk);

  for(int i=0;i<numPoints;i++)
  {
    double ras[4] = {0.0, 0.0, 0.0, 1.0};
    cpNode->GetControlPointWorldCoordinates(i, ras);
    // convert point from ras to ijk
    double ijk[4] = {0.0, 0.0, 0.0, 1.0};
    ras2ijk->MultiplyPoint(ras, ijk);
    // TODO: may need special handling, because this assumes IS acquisition direction
    std::ostringstream ss1, ss2;

    ss1 << ijk[0];
    sl << QString(ss1.str().c_str());
    std::cout << "Coordinate: " << ss1.str().c_str() << std::endl;
    ss2 << ijk[1];
    sl << QString(ss2.str().c_str());
    std::cout << "Coordinate: " << ss2.str().c_str() << std::endl;

  }

  return sl;
}

bool vtkSlicerReportingModuleLogic::WriteLabelAsSegObject(vtkMRMLVolumeNode* srcNode,
  vtkMRMLScalarVolumeNode* labelNode, char* filename)
{

  vtkSmartPointer<vtkImageData> labelImage = labelNode->GetImageData();
  if(!labelImage)
  {
      std::cout << "Failed to get image data!" << std::endl;
      return -1;
  }

  int extent[6];
  labelImage->GetExtent(extent);

  // get the DICOM instance UID(s) for the source series
  std::string uidsString = srcNode->GetAttribute("DICOM.instanceUIDs");
  std::vector<QString> uidVector;
  std::vector<DcmDataset*> dcmDatasetVector;
  char *uids = new char[uidsString.size()+1];
  strcpy(uids,uidsString.c_str());
  char *ptr;
  ptr = strtok(uids, " ");
  while (ptr != NULL)
    {
    vtkDebugMacro("Parsing UID = " << ptr);
    uidVector.push_back(QString(ptr));
    ptr = strtok(NULL, " ");
    }

  if(!this->DICOMDatabase)
    {
    this->InitializeDICOMDatabase();
    }


  // load DcmDataset for each instance UID and put in vector
  for(std::vector<QString>::const_iterator uidIt=uidVector.begin();
      uidIt!=uidVector.end();++uidIt)
    {
    // based on CTK/Libs/DICOM/Core/ctkDICOMDatabase.cpp:loadInstanceHeader()
    QSqlQuery query(this->DICOMDatabase->database());
    query.prepare("SELECT Filename FROM Images WHERE SOPInstanceUID=?");
    query.bindValue(0, *uidIt);
    query.exec();
    if(query.next())
      {
      QString fileName = query.value(0).toString();
      DcmFileFormat fileFormat;
      OFCondition status = fileFormat.loadFile(fileName.toLatin1().data());
      if(status.good())
        {
        std::cout << "Loaded dataset for " << fileName.toLatin1().data();
        dcmDatasetVector.push_back(fileFormat.getDataset());
        }
      }
    }

  // create a DICOM dataset (see http://support.dcmtk.org/docs/mod_dcmdata.html#Examples)
  DcmFileFormat fileformat;
  DcmDataset *dataset = fileformat.getDataset();


  // Get the image orientation information

  // populate the pixel data
  vtkSmartPointer<vtkMatrix4x4> IJKtoRAS = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkSmartPointer<vtkMatrix4x4> RAStoIJK = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkSmartPointer<vtkMatrix4x4> RAStoLPS = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkSmartPointer<vtkMatrix4x4> IJKtoLPS = vtkSmartPointer<vtkMatrix4x4>::New();
  double spacing[3], origin[3];

  labelNode->GetRASToIJKMatrix(RAStoIJK);
  vtkMatrix4x4::Invert(RAStoIJK, IJKtoRAS);
  IJKtoRAS->Transpose();

  for(int i=0;i<3;i++)
  {
    spacing[i]=0;
    for(int j=0;j<3;j++)
    {
      spacing[i]+=IJKtoRAS->GetElement(i,j)*IJKtoRAS->GetElement(i,j);
    }
    if(spacing[i]==0.)
      spacing[i] = 1.;
    spacing[i]=sqrt(spacing[i]);
  }

  for(int i=0;i<3;i++)
  {
    for(int j=0;j<3;j++)
    {
      IJKtoRAS->SetElement(i, j, IJKtoRAS->GetElement(i,j)/spacing[i]);
    }
  }

  RAStoLPS->Identity();
  RAStoLPS->SetElement(0,0,-1);
  RAStoLPS->SetElement(1,1,-1);
  vtkMatrix4x4::Multiply4x4(IJKtoRAS, RAStoLPS, IJKtoLPS);

  origin[0] = IJKtoRAS->GetElement(3,0);
  origin[1] = IJKtoRAS->GetElement(3,1)*-1.;
  origin[2] = IJKtoRAS->GetElement(3,2)*-1.;

  // Patient orientation definition:
  //   http://dabsoft.ch/dicom/3/C.7.6.1.1.1/

  char patientOrientationStr[64];
  sprintf(patientOrientationStr, "%f\\%f\\%f\\%f\\%f\\%f",
          IJKtoLPS->GetElement(0,0), IJKtoLPS->GetElement(1,0),
          IJKtoLPS->GetElement(2,0), IJKtoLPS->GetElement(0,1),
          IJKtoLPS->GetElement(1,1), IJKtoLPS->GetElement(2,1));

  char patientPositionStr[64];
  sprintf(patientPositionStr, "%f\\%f\\%f",
          origin[0], origin[1], origin[2]);

  char pixelSpacingStr[64];
  sprintf(pixelSpacingStr, "%f\\%f",
          spacing[0], spacing[1]);

  char sliceThicknessStr[64];
  sprintf(sliceThicknessStr, "%f",
          spacing[2]);

  // initialize the header
  DcmDataset* dcm0 = dcmDatasetVector[0];
  DcmElement* element;
  DcmItem* item;

  // AF TODO: initialize with meaningful values
  //element = dcm0->getElement(DCM_StudyDate);
  //dcm0->get
  //std::cout << "Study date: " << element->getString() << std::endl;
  //dcm0->findAndGetSequenceItem(DCM_StudyDate, item);

  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  std::cout << "Copied the date" << std::endl;
  this->copyDcmElement(DCM_PatientName, dcm0, dataset);
  this->copyDcmElement(DCM_PatientSex, dcm0, dataset);
  this->copyDcmElement(DCM_PatientAge, dcm0, dataset);
  this->copyDcmElement(DCM_PatientID, dcm0, dataset);
  this->copyDcmElement(DCM_StudyID, dcm0, dataset);
  this->copyDcmElement(DCM_StudyInstanceUID, dcm0, dataset);
  this->copyDcmElement(DCM_AccessionNumber, dcm0, dataset);
  this->copyDcmElement(DCM_StudyTime, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);
  this->copyDcmElement(DCM_StudyDate, dcm0, dataset);

  return 0;

  char uid[128];
  char* seriesUIDStr = dcmGenerateUniqueIdentifier(uid, SITE_SERIES_UID_ROOT);
  dataset->putAndInsertString(DCM_SeriesInstanceUID,seriesUIDStr);
  dataset->putAndInsertString(DCM_InstanceCreatorUID,OFFIS_UID_ROOT);

  char* str;
  dcm0->findAndGetElement(DCM_SeriesInstanceUID, element);
  element->getString(str);
  dataset->putAndInsertString(DCM_FrameOfReferenceUID, str);

  char buf[16] = {0};
  sprintf(buf,"%d", extent[1]+1);
  dataset->putAndInsertString(DCM_Columns,buf);

  sprintf(buf,"%d", extent[3]+1);
  dataset->putAndInsertString(DCM_Rows,buf);

  sprintf(buf,"%d", extent[5]+1);
  dataset->putAndInsertString(DCM_NumberOfFrames,buf);

  dataset->putAndInsertString(DCM_ImageOrientationPatient, patientOrientationStr);
  dataset->putAndInsertString(DCM_ImagePositionPatient, patientPositionStr);
  dataset->putAndInsertString(DCM_PixelSpacing, pixelSpacingStr);
  dataset->putAndInsertString(DCM_SliceThickness, sliceThicknessStr);

  dataset->putAndInsertUint16(DCM_FileMetaInformationVersion,0x0001);
  dataset->putAndInsertString(DCM_SOPClassUID, UID_SegmentationStorage);
  dataset->putAndInsertString(DCM_SOPInstanceUID, "maskUID");

  dataset->putAndInsertString(DCM_Modality,"SEG");
  dataset->putAndInsertString(DCM_SeriesNumber,"1");
  dataset->putAndInsertString(DCM_ImageType,"DERIVED\\PRIMARY");
  dataset->putAndInsertString(DCM_InstanceNumber,"1");

  dataset->putAndInsertString(DCM_SamplesPerPixel,"1");
  dataset->putAndInsertString(DCM_PhotometricInterpretation,"MONOCHROME2");
  dataset->putAndInsertString(DCM_PixelRepresentation,"0");
  dataset->putAndInsertString(DCM_BitsAllocated,"1"); // XIP: 8
  dataset->putAndInsertString(DCM_BitsStored,"1"); // XIP: 8
  dataset->putAndInsertString(DCM_HighBit,"0");

  dataset->putAndInsertString(DCM_LossyImageCompression,"00");

  // segmentation specific header elements
  dataset->putAndInsertString(DCM_SegmentationType, "BINARY");
  dataset->putAndInsertString(DCM_ContentLabel, "3DSlicerSegmentation"); // meaning?
  dataset->putAndInsertString(DCM_ContentDescription, "3D Slicer segmentation result");
  dataset->putAndInsertString(DCM_ContentCreatorName, "3DSlicer");

  // AF TODO: other elements from sup111 table C.8.20-1 ?!?!?

  // segmentation image (?) \ segment sequence
  DcmItem *Item = NULL, *subItem = NULL;
  dataset->findOrCreateSequenceItem(DCM_SegmentSequence, Item);

  // AF TODO: go over all labels and insert separate item for each one
  Item->putAndInsertString(DCM_SegmentNumber, "1");
  Item->putAndInsertString(DCM_SegmentLabel, "Segmentation"); // AF TODO: this should be initialized based on the label value!
  Item->putAndInsertString(DCM_SegmentAlgorithmType, "SEMIAUTOMATIC");
  Item->putAndInsertString(DCM_SegmentAlgorithmName, "Editor");

  //segmentation properties - category
  Item->findOrCreateSequenceItem(DCM_SegmentedPropertyCategoryCodeSequence, subItem);
  subItem->putAndInsertString(DCM_CodeValue,"T-D0050");
  subItem->putAndInsertString(DCM_CodingSchemeDesignator,"SRT");
  subItem->putAndInsertString(DCM_CodeMeaning,"Tissue");

  //segmentation properties - type
  Item->findOrCreateSequenceItem(DCM_SegmentedPropertyTypeCodeSequence, subItem);
  subItem->putAndInsertString(DCM_CodeValue,"M-03010");
  subItem->putAndInsertString(DCM_CodingSchemeDesignator,"SRT");
  subItem->putAndInsertString(DCM_CodeMeaning,"Nodule");

  //Shared functional groups sequence
  dataset->findOrCreateSequenceItem(DCM_SharedFunctionalGroupsSequence, Item);

  //segmentation macro - attributes
  Item->findOrCreateSequenceItem(DCM_SegmentIdentificationSequence, subItem);
  subItem->putAndInsertString(DCM_ReferencedSegmentNumber,"1");

  //segmentation functional group macros
  Item->putAndInsertString(DCM_SliceThickness, sliceThicknessStr);
  Item->putAndInsertString(DCM_PixelSpacing, pixelSpacingStr);

  /*
  const unsigned long itemNum = extent[5];

  //Derivation Image functional group
  Item->findOrCreateSequenceItem(DCM_DerivationImageSequence, subItem, itemNum);
  for(int i=0;i<itemNum+1;i++)
    {
    Item->findAndGetSequenceItem(DCM_DerivationImageSequence, subItem, i);
    subItem->insertSequenceItem()
    }
  */

  OFCondition writeStatus = fileformat.saveFile(filename, EXS_LittleEndianExplicit);
  if(writeStatus.bad())
  {
      std::cout << "Error saving DICOM SEG object: " << writeStatus.text() << std::endl;
      return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

void vtkSlicerReportingModuleLogic::copyDcmElement(const DcmTag& tag, DcmDataset* dcmIn, DcmDataset* dcmOut)
{
  char *str;
  DcmElement* element;
  DcmTag copy = tag;
  std::cout << "Copying tag " << copy.getTagName() << std::endl;
  dcmIn->findAndGetElement(tag, element);
  element->getString(str);
  dcmOut->putAndInsertString(tag, str);
}
