// $Id: ViewerGui.h 299 2011-09-01 15:29:18Z kai_wurm $

/**
* Octomap:
* A  probabilistic, flexible, and compact 3D mapping library for robotic systems.
* @author K. M. Wurm, A. Hornung, University of Freiburg, Copyright (C) 2009-2011.
* @see http://octomap.sourceforge.net/
* License: GNU GPL v2, http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef VIEWERGUI_H
#define VIEWERGUI_H

#include <QtGui/QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QDockWidget>
#include <string>
#include <cmath>
#include "TrajectoryDrawer.h"
#include "PointcloudDrawer.h"
#include "OcTreeDrawer.h"
#include "CameraFollowMode.h"
#include "ViewerWidget.h"
#include "ViewerSettings.h"
#include "ViewerSettingsPanel.h"
#include "ViewerSettingsPanelCamera.h"
#include "ui_ViewerGui.h"

#include <octomap/OcTreeFileIO.h>
#include <octovis/OcTreeRecord.h>

namespace octomap {

  class ViewerGui : public QMainWindow {
    Q_OBJECT
   
  public:
    ViewerGui(const std::string& filename="", QWidget *parent = 0);
    ~ViewerGui();

    static const unsigned int LASERTYPE_URG  = 0;
    static const unsigned int LASERTYPE_SICK = 1;

    // use this drawer id if loading files or none is specified in msg
    static const unsigned int DEFAULT_OCTREE_ID  = 0; 

    public slots:

    void changeTreeDepth(int depth);
    void addNextScans(unsigned scans);
    void gotoFirstScan();

    /* void handleOctomapBinaryMsg(const octomap2::OctomapBinary::ConstPtr& msg); */
    /* void handleMoveMapMsg(const octomap2::MoveMap::ConstPtr& msg); */

    bool isShown();

    private slots:

    // auto-connected Slots (by name))

    void on_actionExit_triggered();
    void on_actionOpen_file_triggered();
    void on_actionOpen_graph_incremental_triggered();
    void on_actionSave_file_triggered();
    void on_actionExport_view_triggered();
    void on_actionExport_sequence_triggered(bool checked);
    void on_actionHelp_triggered();
    void on_actionSettings_triggered();
    void on_actionPrune_tree_triggered();
    void on_actionExpand_tree_triggered();
    void on_actionConvert_ml_tree_triggered();
    void on_actionReload_Octree_triggered();
    void on_actionPrintout_mode_toggled(bool checked);
    void on_actionHeight_map_toggled(bool checked);
    void on_actionSemanticColoring_toggled(bool checked);
    void on_actionStore_camera_triggered();
    void on_actionRestore_camera_triggered();
    void on_actionPointcloud_toggled(bool checked);
    void on_actionTrajectory_toggled(bool checked);
    void on_actionOctree_cells_toggled(bool enabled);
    void on_actionOctree_structure_toggled(bool enabled);
    void on_actionFree_toggled(bool enabled);
    void on_actionSelected_toggled(bool enabled);
    void on_actionAxes_toggled(bool checked);
    void on_actionHideBackground_toggled(bool checked);
    void on_actionClear_triggered();

    void on_action_bg_black_triggered();
    void on_action_bg_white_triggered();
    void on_action_bg_gray_triggered();

    // use it for testcases etc.
    void on_actionTest_triggered();

  signals:
    void updateStatusBar(QString message, int duration);
    void changeNumberOfScans(unsigned scans);
    void changeCurrentScan(unsigned scans);
    void changeResolution(double resolution);
    void changeCamPosition(double x, double y, double z, double lookX, double lookY, double lookZ);

  private:
    /**
     * (Re-)load the data file stored in m_fileName.
     * Depending on the extension, the respective load function is used.
     */
    void openFile();

    /**
     * Reads in a .dat file which consists of single points in ASCII,
     * one point per line, values separated by white spaces
     */
    void openPointcloud();

    /**
     * Opens a .graph file and generates a ScanGraph from it. Afterwards,
     * loadGraph() is called.
     */
    void openGraph(bool completeGraph = true);

    /**
     * Finishes loading a ScanGraph, either from .log or .graph.
     */
    void loadGraph(bool completeGraph = true);

    /**
     * Adds a scan from the graph to the OcTree
     */
    void addNextScan();

    /**
     * Opens a .pc PointCloud
     */
    void openPC();

    // open binary file containing an octree
    void openTree();

    // open "regular" file containing an octree
    void openOcTree();

    // open octree with per-voxel color info
    void openColorOcTree();

    void setOcTreeUISwitches();

    /*!
     * (Re-)generates OcTree from the internally stored ScanGraph
     */
    void generateOctree();
    void showOcTree();

    void showInfo(QString string, bool newline=false);

    void addOctree(AbstractOcTree* tree, int id, pose6d origin);
    void addOctree(AbstractOcTree* tree, int id);
    bool getOctreeRecord(int id, OcTreeRecord*& otr);

    std::map<int, OcTreeRecord> m_octrees;
 
    ScanGraph* m_scanGraph;
    ScanGraph::iterator m_nextScanToAdd;

    Ui::ViewerGuiClass ui;
    ViewerWidget* m_glwidget;
    TrajectoryDrawer* m_trajectoryDrawer;
    PointcloudDrawer* m_pointcloudDrawer;
    CameraFollowMode* m_cameraFollowMode;
    double m_octreeResolution;
    double m_laserMaxRange;
    double m_occupancyThresh; // FIXME: This is not really used at the moment...
    unsigned int m_max_tree_depth;
    unsigned int m_laserType; // SICK or Hokuyo /URG
    bool m_cameraStored;
    QLabel* m_mapSizeStatus;
    QLabel* m_mapMemoryStatus;

    //! Filename of last loaded file, in case it is necessary to reload it
    std::string m_filename;
  };

} // namespace


#endif // VIEWERGUI_H
