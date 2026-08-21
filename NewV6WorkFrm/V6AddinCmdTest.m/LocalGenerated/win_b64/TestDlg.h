

// BEGIN OF HEADER CODE
// ----------------------------------------
    
#ifndef TestDlg_h
#define TestDlg_h

#include "CATDlgDialog.h"
class CATDlgFrame;

// Begin of User Code
            #include <windows.h>
            #include "CATDlgFrame.h"
// End of User Code

class  TestDlg : public CATDlgDialog {
DeclareResource(TestDlg, CATDlgDialog)

public:
	TestDlg(CATDialog * iParent, const CATString& iDialogName);
	
	virtual ~TestDlg();
	void Build();

	// Copy ctor and assignment operators are declared but not defined by infra
	TestDlg(const TestDlg&);
	TestDlg& operator= (const TestDlg&);
	
private:
CATDlgFrame* _QtTreeTableHost;

// Begin of User Code
public:
CATDlgFrame *GetQtTreeTableHost()
{
    return _QtTreeTableHost;
}
// End of User Code

};

#endif
