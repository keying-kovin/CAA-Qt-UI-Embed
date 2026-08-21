

// ----------------------------------------
// BEGIN OF IMPLEMENTATION CODE
// ----------------------------------------
#include "TestDlg.h"
#include "CATDlgGridConstraints.h"
#include "CATDlgFrame.h"

// Begin of User Code

// End of User Code

TestDlg::TestDlg(CATDialog* iParent, const CATString& iName)
    :CATDlgDialog(iParent,iName,0
|CATDlgWndNoButton
| CATDlgGridLayout
)
{

_QtTreeTableHost = NULL;

// Begin of User Code

// End of User Code

}

TestDlg::~TestDlg()
{

// Begin of User Code

// End of User Code
_QtTreeTableHost = NULL;

}



void TestDlg::Build() 
{
int style=0;

// Begin of User Code

// End of User Code
_QtTreeTableHost=new CATDlgFrame(this,(const char*)"QtTreeTableHost",0|CATDlgFraNoTitle|CATDlgFraNoFrame|CATDlgGridLayout);
this->SetGridRowResizable(0,1);
this->SetGridRowResizable(1,1);
this->SetGridColumnResizable(0,1);
this->SetGridColumnResizable(1,1);
_QtTreeTableHost->SetGridConstraints(0,0,1,1,0|CATGRID_LEFT|CATGRID_RIGHT|CATGRID_TOP|CATGRID_BOTTOM);

// Begin of User Code

// End of User Code

}


// You can put here the implementation of class methods

// Begin of User Code

// End of User Code


// End of implementation of class methods
