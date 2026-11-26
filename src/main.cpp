// FseqModelViewer: play one model's channels from an .fseq file.
//
// The show folder is loaded with xLights' own core (HeadlessRenderContext), so
// every model type, start-channel form and node layout matches xLights.

#include <wx/wx.h>
#include <wx/config.h>
#include <wx/dcbuffer.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/slider.h>

#include "render/HeadlessRenderContext.h"
#include "render/FSEQFile.h"
#include "models/Model.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct NodePoint {
    uint32_t node;
    float x;
    float y;
};

enum class LayoutMode { Screen = 0, Buffer = 1 };

class PreviewPanel : public wxPanel {
public:
    explicit PreviewPanel(wxWindow* parent) :
        wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(640, 480), wxFULL_REPAINT_ON_RESIZE) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &PreviewPanel::OnPaint, this);
    }

    void SetPoints(std::vector<NodePoint> pts) {
        _points = std::move(pts);
        _minX = _minY = std::numeric_limits<float>::max();
        _maxX = _maxY = std::numeric_limits<float>::lowest();
        for (const auto& p : _points) {
            _minX = std::min(_minX, p.x);
            _maxX = std::max(_maxX, p.x);
            _minY = std::min(_minY, p.y);
            _maxY = std::max(_maxY, p.y);
        }
        Refresh(false);
    }

    void SetColors(std::vector<xlColor> colors) {
        _colors = std::move(colors);
        Refresh(false);
    }

    void SetMessage(const wxString& msg) {
        _message = msg;
        Refresh(false);
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(*wxBLACK_BRUSH);
        dc.Clear();

        const wxSize sz = GetClientSize();
        if (_points.empty()) {
            dc.SetTextForeground(*wxLIGHT_GREY);
            dc.DrawLabel(_message, wxRect(sz), wxALIGN_CENTER);
            return;
        }

        const float margin = 20.0f;
        const float spanX = std::max(_maxX - _minX, 1.0f);
        const float spanY = std::max(_maxY - _minY, 1.0f);
        const float scale = std::min((sz.x - 2 * margin) / spanX, (sz.y - 2 * margin) / spanY);
        const float offX = (sz.x - spanX * scale) / 2.0f;
        const float offY = (sz.y - spanY * scale) / 2.0f;

        // Rough node spacing so dots fill but don't swamp the model.
        const float spacing = scale * std::sqrt(spanX * spanY / std::max<size_t>(_points.size(), 1));
        const int radius = std::clamp((int)(spacing * 0.35f), 1, 10);

        dc.SetPen(*wxTRANSPARENT_PEN);
        for (const auto& p : _points) {
            xlColor c = p.node < _colors.size() ? _colors[p.node] : xlBLACK;
            if (c.red == 0 && c.green == 0 && c.blue == 0) {
                c = xlColor(30, 30, 30); // off nodes stay visible as dim dots
            }
            dc.SetBrush(wxBrush(wxColour(c.red, c.green, c.blue)));
            const int px = (int)(offX + (p.x - _minX) * scale);
            const int py = (int)(sz.y - (offY + (p.y - _minY) * scale)); // model Y is up
            if (radius <= 2) {
                dc.DrawRectangle(px - radius, py - radius, radius * 2, radius * 2);
            } else {
                dc.DrawCircle(px, py, radius);
            }
        }
    }

    std::vector<NodePoint> _points;
    std::vector<xlColor> _colors;
    float _minX = 0, _maxX = 0, _minY = 0, _maxY = 0;
    wxString _message = "Choose a show folder, an .fseq file and a model.";
};

class ViewerFrame : public wxFrame {
public:
    ViewerFrame() :
        wxFrame(nullptr, wxID_ANY, "FSEQ Model Viewer", wxDefaultPosition, wxSize(1000, 750)) {
        auto* root = new wxPanel(this);
        auto* sizer = new wxBoxSizer(wxVERTICAL);

        auto* row1 = new wxBoxSizer(wxHORIZONTAL);
        auto* showBtn = new wxButton(root, wxID_ANY, "Show Folder...");
        _showLabel = new wxStaticText(root, wxID_ANY, "(none)");
        auto* fseqBtn = new wxButton(root, wxID_ANY, "FSEQ...");
        _fseqLabel = new wxStaticText(root, wxID_ANY, "(none)");
        row1->Add(showBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        row1->Add(_showLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
        row1->Add(fseqBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        row1->Add(_fseqLabel, 1, wxALIGN_CENTER_VERTICAL);

        auto* row2 = new wxBoxSizer(wxHORIZONTAL);
        _modelChoice = new wxChoice(root, wxID_ANY, wxDefaultPosition, wxSize(300, -1));
        _layoutChoice = new wxChoice(root, wxID_ANY);
        _layoutChoice->Append("Layout view");
        _layoutChoice->Append("Buffer view");
        _layoutChoice->SetSelection(0);
        _playBtn = new wxButton(root, wxID_ANY, "Play");
        _infoLabel = new wxStaticText(root, wxID_ANY, "");
        row2->Add(new wxStaticText(root, wxID_ANY, "Model:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        row2->Add(_modelChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
        row2->Add(_layoutChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
        row2->Add(_playBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
        row2->Add(_infoLabel, 1, wxALIGN_CENTER_VERTICAL);

        _preview = new PreviewPanel(root);

        auto* row3 = new wxBoxSizer(wxHORIZONTAL);
        _slider = new wxSlider(root, wxID_ANY, 0, 0, 1);
        _timeLabel = new wxStaticText(root, wxID_ANY, "0:00.000 / 0:00.000", wxDefaultPosition, wxSize(170, -1));
        row3->Add(_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        row3->Add(_timeLabel, 0, wxALIGN_CENTER_VERTICAL);

        sizer->Add(row1, 0, wxEXPAND | wxALL, 8);
        sizer->Add(row2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        sizer->Add(_preview, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
        sizer->Add(row3, 0, wxEXPAND | wxALL, 8);
        root->SetSizer(sizer);

        showBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseShowFolder(); });
        fseqBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseFseq(); });
        _modelChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectModel(); });
        _layoutChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectModel(); });
        _playBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TogglePlay(); });
        _slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { ShowFrame(_slider->GetValue()); });
        _timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnTick(); });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { _timer.Stop(); SaveConfig(); e.Skip(); });

        RestoreConfig();
    }

    void LoadFromArgs(const wxString& show, const wxString& fseq, const wxString& model) {
        if (!LoadShow(show)) {
            return;
        }
        if (!fseq.IsEmpty()) {
            LoadFseq(fseq);
        }
        if (!model.IsEmpty()) {
            int idx = _modelChoice->FindString(model, false);
            if (idx == wxNOT_FOUND) {
                wxMessageBox("Model not found: " + model, "Model", wxICON_WARNING, this);
                return;
            }
            _modelChoice->SetSelection(idx);
            SelectModel();
        }
    }

private:
    void ChooseShowFolder() {
        wxDirDialog dlg(this, "Choose the xLights show folder", _showDir);
        if (dlg.ShowModal() == wxID_OK) {
            LoadShow(dlg.GetPath());
        }
    }

    void ChooseFseq() {
        wxString dir = _fseqPath.IsEmpty() ? _showDir : wxFileName(_fseqPath).GetPath();
        wxFileDialog dlg(this, "Choose an .fseq file", dir, "", "FSEQ files (*.fseq)|*.fseq|All files (*.*)|*.*",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            LoadFseq(dlg.GetPath());
        }
    }

    bool LoadShow(const wxString& dir) {
        StopPlay();
        wxBusyCursor busy;
        _model = nullptr;
        _modelChoice->Clear();
        _preview->SetPoints({});
        _show.reset();

        auto ctx = std::make_unique<HeadlessRenderContext>();
        if (!ctx->LoadShowFolder(dir.ToStdString(wxConvUTF8))) {
            wxMessageBox("Could not load xlights_rgbeffects.xml from\n" + dir, "Show folder", wxICON_ERROR, this);
            _showLabel->SetLabel("(none)");
            return false;
        }
        _show = std::move(ctx);
        _showDir = dir;
        _showLabel->SetLabel(dir);

        wxArrayString names;
        for (auto it = _show->AllModels.begin(); it != _show->AllModels.end(); ++it) {
            if (it->second != nullptr && it->second->GetDisplayAs() != DisplayAsType::ModelGroup) {
                names.Add(wxString::FromUTF8(it->first));
            }
        }
        names.Sort([](const wxString& a, const wxString& b) { return a.CmpNoCase(b); });
        _modelChoice->Append(names);
        Layout();
        return true;
    }

    bool LoadFseq(const wxString& path) {
        StopPlay();
        std::unique_ptr<FSEQFile> f(FSEQFile::openFSEQFile(path.ToStdString(wxConvUTF8)));
        if (!f) {
            wxMessageBox("Could not open\n" + path, "FSEQ", wxICON_ERROR, this);
            return false;
        }
        const uint32_t maxCh = std::max<uint32_t>(f->getMaxChannel(), (uint32_t)f->getChannelCount());
        std::vector<std::pair<uint32_t, uint32_t>> rng{ { 0, maxCh } };
        f->prepareRead(rng);
        _frameBuf.assign(maxCh, 0);
        _fseq = std::move(f);
        _fseqPath = path;
        _fseqLabel->SetLabel(wxFileName(path).GetFullName());

        const int frames = (int)_fseq->getNumFrames();
        _slider->SetRange(0, std::max(frames - 1, 1));
        _slider->SetValue(0);
        _timer.Stop();
        Layout();
        UpdateInfo();
        ShowFrame(0);
        return true;
    }

    void SelectModel() {
        _model = nullptr;
        if (!_show || _modelChoice->GetSelection() == wxNOT_FOUND) {
            _preview->SetPoints({});
            UpdateInfo();
            return;
        }
        const std::string name = _modelChoice->GetStringSelection().ToStdString(wxConvUTF8);
        _model = _show->GetModel(name);
        if (_model == nullptr) {
            _preview->SetPoints({});
            UpdateInfo();
            return;
        }

        const bool buffer = _layoutChoice->GetSelection() == (int)LayoutMode::Buffer;
        std::vector<NodePoint> pts;
        const uint32_t nodes = _model->GetNodeCount();
        for (uint32_t n = 0; n < nodes; ++n) {
            if (buffer) {
                std::vector<xlPoint> c;
                _model->GetNodeCoords((int)n, c);
                for (const auto& p : c) pts.push_back({ n, (float)p.x, (float)p.y });
            } else {
                std::vector<std::tuple<float, float, float>> c;
                _model->GetNode3DScreenCoords((int)n, c);
                for (const auto& p : c) pts.push_back({ n, std::get<0>(p), std::get<1>(p) });
            }
        }
        _preview->SetPoints(std::move(pts));
        UpdateInfo();
        ShowFrame(_slider->GetValue());
    }

    void ShowFrame(int frame) {
        if (!_fseq || !_model) {
            UpdateTime(frame);
            return;
        }
        std::fill(_frameBuf.begin(), _frameBuf.end(), 0);
        if (frame >= 0 && (uint64_t)frame < _fseq->getNumFrames()) {
            std::unique_ptr<FSEQFile::FrameData> d(_fseq->getFrame((uint32_t)frame));
            if (d) {
                d->readFrame(_frameBuf.data(), (uint32_t)_frameBuf.size());
            }
        }

        const uint32_t nodes = _model->GetNodeCount();
        const size_t per = (size_t)std::max(_model->GetChanCountPerNode(), 1);
        std::vector<xlColor> colors(nodes, xlBLACK);
        for (uint32_t n = 0; n < nodes; ++n) {
            const int32_t start = _model->NodeStartChannel(n);
            if (start >= 0 && (size_t)start + per <= _frameBuf.size()) {
                _model->SetNodeChannelValues(n, &_frameBuf[start]);
                colors[n] = _model->GetNodeColor(n);
            }
        }
        _preview->SetColors(std::move(colors));
        UpdateTime(frame);
    }

    void TogglePlay() {
        if (_timer.IsRunning()) {
            StopPlay();
        } else if (_fseq) {
            _timer.Start(std::max(_fseq->getStepTime(), 10));
            _playBtn->SetLabel("Pause");
        }
    }

    void StopPlay() {
        _timer.Stop();
        _playBtn->SetLabel("Play");
    }

    void OnTick() {
        int next = _slider->GetValue() + 1;
        if (!_fseq || (uint64_t)next >= _fseq->getNumFrames()) {
            next = 0; // loop
        }
        _slider->SetValue(next);
        ShowFrame(next);
    }

    static wxString FormatMS(int64_t ms) {
        return wxString::Format("%d:%02d.%03d", (int)(ms / 60000), (int)((ms / 1000) % 60), (int)(ms % 1000));
    }

    void UpdateTime(int frame) {
        const int step = _fseq ? _fseq->getStepTime() : 50;
        const int64_t total = _fseq ? (int64_t)_fseq->getNumFrames() * step : 0;
        _timeLabel->SetLabel(FormatMS((int64_t)frame * step) + " / " + FormatMS(total));
    }

    void UpdateInfo() {
        wxString s;
        if (_model) {
            s += wxString::Format("%s, %u nodes, ch %u-%u", wxString::FromUTF8(_model->GetDisplayAsString()),
                                  _model->GetNodeCount(), _model->GetFirstChannel() + 1,
                                  _model->GetFirstChannel() + _model->GetChanCount());
        }
        if (_fseq) {
            if (!s.IsEmpty()) s += "   |   ";
            s += wxString::Format("fseq: %llu ch, %llu frames @ %d ms",
                                  (unsigned long long)_fseq->getChannelCount(),
                                  (unsigned long long)_fseq->getNumFrames(), _fseq->getStepTime());
            if (_model && _model->GetFirstChannel() + _model->GetChanCount() > _frameBuf.size()) {
                s += "  (model is past the end of the fseq data)";
            }
        }
        _infoLabel->SetLabel(s);
        if (_show && !_model) {
            _preview->SetMessage("Choose a model.");
        }
    }

    void RestoreConfig() {
        wxConfigBase* cfg = wxConfigBase::Get();
        wxString show, fseq, model;
        long layout = 0;
        cfg->Read("ShowDir", &show);
        cfg->Read("Fseq", &fseq);
        cfg->Read("Model", &model);
        cfg->Read("Layout", &layout);
        _layoutChoice->SetSelection(layout == 1 ? 1 : 0);
        if (!show.IsEmpty() && wxDirExists(show) && LoadShow(show)) {
            if (!fseq.IsEmpty() && wxFileExists(fseq)) {
                LoadFseq(fseq);
            }
            int idx = _modelChoice->FindString(model, true);
            if (idx != wxNOT_FOUND) {
                _modelChoice->SetSelection(idx);
                SelectModel();
            }
        }
    }

    void SaveConfig() {
        wxConfigBase* cfg = wxConfigBase::Get();
        cfg->Write("ShowDir", _showDir);
        cfg->Write("Fseq", _fseqPath);
        cfg->Write("Model", _modelChoice->GetStringSelection());
        cfg->Write("Layout", (long)_layoutChoice->GetSelection());
        cfg->Flush();
    }

    std::unique_ptr<HeadlessRenderContext> _show;
    std::unique_ptr<FSEQFile> _fseq;
    std::vector<uint8_t> _frameBuf;
    Model* _model = nullptr;
    wxString _showDir;
    wxString _fseqPath;
    wxTimer _timer;

    wxStaticText* _showLabel = nullptr;
    wxStaticText* _fseqLabel = nullptr;
    wxStaticText* _infoLabel = nullptr;
    wxStaticText* _timeLabel = nullptr;
    wxChoice* _modelChoice = nullptr;
    wxChoice* _layoutChoice = nullptr;
    wxButton* _playBtn = nullptr;
    wxSlider* _slider = nullptr;
    PreviewPanel* _preview = nullptr;
};

class ViewerApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("FseqModelViewer");
        SetVendorName("derwin12");
        InitLogging();
        auto* frame = new ViewerFrame();
        frame->Show();
        // Optional: FseqModelViewer.exe <showdir> [<fseq> [<model>]]
        if (argc > 1) {
            frame->LoadFromArgs(argv[1], argc > 2 ? argv[2] : wxString(), argc > 3 ? argv[3] : wxString());
        }
        return true;
    }

private:
    // The core logs through these named loggers (spdlog::get("curl") etc.) and
    // dereferences them without a null check, so they must exist.
    static void InitLogging() {
        const std::string path = (wxFileName::GetTempDir() + "/FseqModelViewer.log").ToStdString(wxConvUTF8);
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 5 * 1024 * 1024, 2);
        auto main = std::make_shared<spdlog::logger>("viewer", sink);
        spdlog::set_default_logger(main);
        spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%n %l] %v");
        for (const char* name : { "render", "curl", "opengl", "job", "work" }) {
            auto l = std::make_shared<spdlog::logger>(name, sink);
            l->set_level(spdlog::level::warn);
            spdlog::register_logger(l);
        }
        spdlog::flush_every(std::chrono::seconds(2));
    }
};

} // namespace

wxIMPLEMENT_APP(ViewerApp);
