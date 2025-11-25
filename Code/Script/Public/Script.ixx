export module Script;

export class Script final
{
public:

    Script();
    ~Script();

    bool initialize();
    void update(double deltaSec);

private:

    friend class UI;
    void updateUI(double deltaSec);
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU5")
    Script script;
#pragma warning(default: 4075)
}