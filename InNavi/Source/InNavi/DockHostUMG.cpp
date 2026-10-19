#include "DockHostUMG.h"

UDockHostWidget::UDockHostWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The host must not swallow clicks on the empty central area - only its chrome and content take input.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Layout = MakeShared<FDockLayout>();
}

void UDockHostWidget::SetMetrics(const FDockMetrics& InMetrics)
{
	Metrics = InMetrics;
	if (Host.IsValid())
	{
		Host->SetMetrics(Metrics);
	}
}

void UDockHostWidget::RegisterPanel(FName Id, const FText& Title, UWidget* Content, bool bScrollable)
{
	if (!Content)
	{
		return;
	}
	PanelContents.Add(Id, Content);
	FPanelInfo Info;
	Info.Title = Title;
	Info.bScrollable = bScrollable;
	PanelInfos.Add(Id, Info);

	if (Host.IsValid())
	{
		Host->RegisterPanel(Id, Title, Content->TakeWidget(), bScrollable);
	}
}

void UDockHostWidget::NotifyLayoutChanged()
{
	if (Host.IsValid())
	{
		Host->NotifyLayoutChanged();
	}
}

bool UDockHostWidget::IsScreenPositionOverPanel(FName Id, const FVector2D& ScreenPosition) const
{
	return Host.IsValid() && Host->IsScreenPositionOverPanel(Id, ScreenPosition);
}

void UDockHostWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Host.Reset();
}

TSharedRef<SWidget> UDockHostWidget::RebuildWidget()
{
	Host = SNew(SDockHost)
		.Layout(Layout)
		.Metrics(Metrics)
		.OnLayoutChanged_Lambda([this]() { OnLayoutChangedNative.ExecuteIfBound(); })
		.OnPanelClosed_Lambda([this](FName Panel) { OnPanelClosedNative.ExecuteIfBound(Panel); });

	for (const TPair<FName, TObjectPtr<UWidget>>& Pair : PanelContents)
	{
		if (const FPanelInfo* Info = PanelInfos.Find(Pair.Key))
		{
			if (Pair.Value)
			{
				Host->RegisterPanel(Pair.Key, Info->Title, Pair.Value->TakeWidget(), Info->bScrollable);
			}
		}
	}
	return Host.ToSharedRef();
}
