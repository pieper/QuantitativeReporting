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

// Slicer
#include "vtkMRMLScene.h"
#include "vtkMRMLScalarVolumeNode.h"
#include "vtkSmartPointer.h"
#include "vtkMatrix4x4.h"

// CTK
#include "ctkDICOMDatabase.h"

// Qt
#include <QSqlQuery>

// VTK
#include <vtkImageData.h>

ctkDICOMDatabase* InitializeDICOMDatabase();
void copyDcmElement(const DcmTag& tag, DcmDataset* dcmIn, DcmDataset* dcmOut);

int main(int argc, char** argv)
{
    if(argc<2)
      return 0;
      
    vtkSmartPointer<vtkMRMLScene> scene = vtkSmartPointer<vtkMRMLScene>::New();
    scene->SetURL(argv[1]);
    if(!scene->Import())
      {
      std::cerr << "Error loading the scene!" << std::endl;
      return -1;
      }
    std::cout << "Test scene loaded!" << std::endl;

    vtkSmartPointer<vtkMRMLScalarVolumeNode> vol = 
      vtkMRMLScalarVolumeNode::SafeDownCast(scene->GetNodeByID("vtkMRMLScalarVolumeNode1"));

    vtkSmartPointer<vtkMRMLScalarVolumeNode> labelNode = 
      vtkMRMLScalarVolumeNode::SafeDownCast(scene->GetNodeByID("vtkMRMLScalarVolumeNode2"));
    vtkImageData* labelImage = labelNode->GetImageData();
    int extent[6];
    labelImage->GetExtent(extent);

    ctkDICOMDatabase *db = InitializeDICOMDatabase();
    if(!db)
      {
      std::cerr << "Failed to initialize DICOM db!" << std::endl;
      return -1;
      }

    // create a DICOM dataset (see
    // http://support.dcmtk.org/docs/mod_dcmdata.html#Examples)
    std::string uidsString = vol->GetAttribute("DICOM.instanceUIDs");
    std::vector<std::string> uidVector;
    std::vector<DcmDataset*> dcmDatasetVector;
    char *uids = new char[uidsString.size()+1];
    strcpy(uids,uidsString.c_str());
    char *ptr;
    ptr = strtok(uids, " ");
    while (ptr != NULL)
      {
      std::cout << "Parsing UID = " << ptr << std::endl;
      uidVector.push_back(std::string(ptr));
      ptr = strtok(NULL, " ");
      }

    for(std::vector<std::string>::const_iterator uidIt=uidVector.begin();
      uidIt!=uidVector.end();++uidIt)
      {
      QSqlQuery query(db->database());
      query.prepare("SELECT Filename FROM Images WHERE SOPInstanceUID=?");
      query.bindValue(0, QString((*uidIt).c_str()));
      query.exec();
      if(query.next())
        {
        QString fileName = query.value(0).toString();
        DcmFileFormat fileFormat;
        OFCondition status = fileFormat.loadFile(fileName.toLatin1().data());
        if(status.good())
          {
          std::cout << "Loaded dataset for " << fileName.toLatin1().data() << std::endl;
          dcmDatasetVector.push_back(fileFormat.getAndRemoveDataset());
          //DcmElement *element;
          //OFCondition res = fileFormat.getDataset()->findAndGetElement(DCM_SOPClassUID, element);
          //std::cout << "findAndGetElement is ok" << std::endl;
          }
        }
      }

    
    // Get the image orientation information
    vtkSmartPointer<vtkMatrix4x4> IJKtoRAS = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkSmartPointer<vtkMatrix4x4> RAStoIJK = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkSmartPointer<vtkMatrix4x4> RAStoLPS = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkSmartPointer<vtkMatrix4x4> IJKtoLPS = vtkSmartPointer<vtkMatrix4x4>::New();
    double spacing[3], origin[3], colDir[3], rowDir[3];

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

    origin[0] = IJKtoLPS->GetElement(3,0);
    origin[1] = IJKtoLPS->GetElement(3,1);
    origin[2] = IJKtoLPS->GetElement(3,2);

    colDir[0] = IJKtoLPS->GetElement(0,0);
    colDir[1] = IJKtoLPS->GetElement(0,1);
    colDir[2] = IJKtoLPS->GetElement(0,2);

    rowDir[0] = IJKtoLPS->GetElement(1,0);
    rowDir[1] = IJKtoLPS->GetElement(1,1);
    rowDir[2] = IJKtoLPS->GetElement(1,2);

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


    // create a DICOM dataset (see
    // http://support.dcmtk.org/docs/mod_dcmdata.html#Examples)
    DcmDataset *dcm0 = dcmDatasetVector[0];

    DcmFileFormat fileformatOut;
    DcmDataset *dataset = fileformatOut.getDataset(), *datasetIn;
    
    // writeSegHeader
    copyDcmElement(DCM_StudyDate, dcm0, dataset);
    copyDcmElement(DCM_PatientName, dcm0, dataset);
    copyDcmElement(DCM_PatientSex, dcm0, dataset);
    copyDcmElement(DCM_PatientAge, dcm0, dataset);
    copyDcmElement(DCM_PatientID, dcm0, dataset);
    copyDcmElement(DCM_StudyID, dcm0, dataset);
    copyDcmElement(DCM_StudyInstanceUID, dcm0, dataset);
    copyDcmElement(DCM_AccessionNumber, dcm0, dataset);

    copyDcmElement(DCM_StudyTime, dcm0, dataset);

    dataset->putAndInsertUint16(DCM_FileMetaInformationVersion,0x0001);
    dataset->putAndInsertString(DCM_SOPClassUID, UID_SegmentationStorage);

    char uid[128];

    dataset->putAndInsertString(DCM_SOPInstanceUID, dcmGenerateUniqueIdentifier(uid, SITE_INSTANCE_UID_ROOT));
    dataset->putAndInsertString(DCM_Modality,"SEG");
    dataset->putAndInsertString(DCM_ImageType,"DERIVED\\PRIMARY");
    dataset->putAndInsertString(DCM_SeriesNumber,"1");
    dataset->putAndInsertString(DCM_InstanceNumber,"1");

    char *seriesUIDStr = dcmGenerateUniqueIdentifier(uid, SITE_SERIES_UID_ROOT);
    dataset->putAndInsertString(DCM_SeriesInstanceUID,seriesUIDStr);
    dataset->putAndInsertString(DCM_InstanceCreatorUID,OFFIS_UID_ROOT);

    dataset->putAndInsertString(DCM_FrameOfReferenceUID, seriesUIDStr);

    char buf[16] = {0};
    sprintf(buf,"%d", extent[1]+1);
    dataset->putAndInsertString(DCM_Columns,buf);
    sprintf(buf,"%d", extent[3]+1);
    dataset->putAndInsertString(DCM_Rows,buf);
    sprintf(buf,"%d", extent[5]+1);
    dataset->putAndInsertString(DCM_NumberOfFrames,buf);

    dataset->putAndInsertString(DCM_SamplesPerPixel,"1");
    dataset->putAndInsertString(DCM_PhotometricInterpretation,"MONOCHROME2");
    dataset->putAndInsertString(DCM_PixelRepresentation,"0");

    dataset->putAndInsertString(DCM_BitsAllocated,"1"); // XIP: 8
    dataset->putAndInsertString(DCM_BitsStored,"1"); // XIP: 8
    dataset->putAndInsertString(DCM_HighBit,"0");
    dataset->putAndInsertString(DCM_LossyImageCompression,"00");

    // writeSegFrames
    dataset->putAndInsertString(DCM_ImageOrientationPatient, patientOrientationStr);
    dataset->putAndInsertString(DCM_ImagePositionPatient, patientPositionStr);
    dataset->putAndInsertString(DCM_PixelSpacing, pixelSpacingStr);
    dataset->putAndInsertString(DCM_SliceThickness, sliceThicknessStr);

    // segmentation specific header elements
    dataset->putAndInsertString(DCM_SegmentationType, "BINARY");
    dataset->putAndInsertString(DCM_ContentLabel, "3DSlicerSegmentation"); // meaning?
    dataset->putAndInsertString(DCM_ContentDescription, "3D Slicer segmentation result");
    dataset->putAndInsertString(DCM_ContentCreatorName, "3DSlicer");

    // AF TODO: other elements from sup111 table C.8.20-1 ?!?!?

    // segmentation image (?) \ segment sequence
    // segment sequence [0620,0002]
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
    
    const unsigned long itemNum = extent[5];

    //Derivation Image functional group
    Item->findOrCreateSequenceItem(DCM_DerivationImageSequence, subItem, itemNum);
    for(int i=0;i<itemNum+1;i++)
      {
      Item->findAndGetSequenceItem(DCM_DerivationImageSequence, subItem, i);
      DcmDataset *sliceDataset = dcmDatasetVector[i];
      char *str;
      DcmElement *element;
      sliceDataset->findAndGetElement(DCM_SOPClassUID, element);
      element->getString(str);
      subItem->putAndInsertString(DCM_SOPClassUID, str);

      sliceDataset->findAndGetElement(DCM_SOPInstanceUID, element);
      element->getString(str);
      subItem->putAndInsertString(DCM_SOPInstanceUID, str);

      }


    // per-frame functional groups
    dataset->findOrCreateSequenceItem(DCM_PerFrameFunctionalGroupsSequence, Item, itemNum);
	
    for (int i=0;i<itemNum+1;i++)
      {
      char buf[64];

      // get ImagePositionPatient and ImageOrientationPatient from the
      // original DICOM
      char *str;
      DcmElement *element;
      dcmDatasetVector[i]->findAndGetElement(DCM_ImagePositionPatient, element);
      element->getString(str);
      std::cout << "Read position patient: " << str << std::endl;
      dcmDatasetVector[i]->findAndGetElement(DCM_ImageOrientationPatient, element);
      element->getString(str);
      std::cout << "Read orientation patient: " << str << std::endl;


      dataset->findAndGetSequenceItem(DCM_PerFrameFunctionalGroupsSequence,Item,i);

      Item->findOrCreateSequenceItem(DCM_FrameContentSequence, subItem);
      subItem->putAndInsertString(DCM_StackID,"1"); 

      sprintf(buf, "%d", i+1);
      subItem->putAndInsertString(DCM_InStackPositionNumber,buf);

      DcmItem *seqItem = NULL;

      sprintf(buf, "%f\\%f\\%f", origin[0], origin[1], origin[2]+i*spacing[2]);
      subItem->findOrCreateSequenceItem(DCM_PlanePositionSequence, seqItem);
      seqItem->putAndInsertString(DCM_ImagePositionPatient,buf);
      std::cout << "Write position: " << buf << std::endl;

      sprintf(buf, "%f\\%f\\%f\\%f\\%f\\%f", colDir[0], colDir[1], colDir[2], rowDir[0], rowDir[1], rowDir[2]);
      subItem->findOrCreateSequenceItem(DCM_PlaneOrientationSequence, seqItem);
      seqItem->putAndInsertString(DCM_ImageOrientationPatient, buf);
      std::cout << "Write orientation: " << buf << std::endl;
      }//Per-Frame Functional Groups Sequence information

    // pixel data
    int nbytes = (int) (float((extent[1]+1)*(extent[3]+1)*(extent[5]+1))/8.);
    int total = 0;
    unsigned char *pixelArray = new unsigned char[nbytes];
    for(int i=0;i<nbytes;i++)
      pixelArray[i] = 0;

    for(int i=0;i<extent[1]+1;i++)
      {
      for(int j=0;j<extent[3]+1;j++)
        {
        for(int k=0;k<extent[5]+1;k++)
          {
          int byte = total / 8, bit = total % 8;
          total++;
          pixelArray[byte] |= ((unsigned char) labelImage->GetScalarComponentAsFloat(i,j,k,0)) << bit;
          }
        }
      }

    
    dataset->putAndInsertUint8Array(DCM_PixelData, pixelArray, nbytes);//write pixels

    delete [] pixelArray;

    OFCondition status = fileformatOut.saveFile("output.dcm", EXS_LittleEndianExplicit);
    if(status.bad())
      std::cout << "Error writing: " << status.text() << std::endl;

    return 0;
}

ctkDICOMDatabase* InitializeDICOMDatabase()
{
    std::cout << "Reporting will use database at this location: /Users/fedorov/DICOM_db" << std::endl;

    bool success = false;

    const char *dbPath = "/Users/fedorov/DICOM_db/ctkDICOM.sql";

      {
      ctkDICOMDatabase* DICOMDatabase = new ctkDICOMDatabase();
      DICOMDatabase->openDatabase(dbPath,"Reporting");
      if(DICOMDatabase->isOpen())
        return DICOMDatabase;
      }
    return NULL;
}

void copyDcmElement(const DcmTag& tag, DcmDataset* dcmIn, DcmDataset* dcmOut)
{
  char *str;
  DcmElement* element;
  DcmTag copy = tag;
  std::cout << "Copying tag " << copy.getTagName() << std::endl;
  dcmIn->findAndGetElement(tag, element);
  element->getString(str);
  dcmOut->putAndInsertString(tag, str);
}
